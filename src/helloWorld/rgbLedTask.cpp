/**
 * @file rgbLedTask.cpp
 * @brief RGB LED control implementation using ESP32 RMT peripheral
 * 
 * Implementation of WS2812B RGB LED control using the ESP32 RMT (Remote Control)
 * peripheral for precise timing control required by the WS2812B protocol.
 */

#include "rgbLedTask.h"
#include <esp_log.h>
#include <driver/rmt_tx.h>
#include <string.h>

static const char *TAG = "RGB_LED";

/** @defgroup Hardware Configuration
 * @{
 */
#define LED_PIN         38    /**< GPIO pin for RGB LED (Lolin S3 board) */
#define NUM_LEDS        1     /**< Number of LEDs in the strip */
/** @} */

/** @defgroup WS2812B Timing Constants
 * WS2812B protocol timing specifications in nanoseconds
 * @{
 */
#define WS2812_T0H_NS   350   /**< Logic 0 high time (ns) */
#define WS2812_T0L_NS   900   /**< Logic 0 low time (ns) */
#define WS2812_T1H_NS   900   /**< Logic 1 high time (ns) */
#define WS2812_T1L_NS   350   /**< Logic 1 low time (ns) */
#define WS2812_RESET_US 280   /**< Reset pulse duration (μs) */
/** @} */

/** @defgroup Module Variables
 * @{
 */
static rmt_channel_handle_t led_channel = NULL;  /**< RMT channel handle for LED control */
static rmt_encoder_handle_t led_encoder = NULL;  /**< RMT encoder handle for WS2812B protocol */
static volatile bool led_enabled = true;          /**< Flag to enable/disable LED blinking */
/** @} */

/**
 * @brief RGB color structure
 * 
 * Represents a color in RGB format with 8-bit channels
 */
typedef struct {
    uint8_t red;    /**< Red component (0-255) */
    uint8_t green;  /**< Green component (0-255) */
    uint8_t blue;   /**< Blue component (0-255) */
} rgb_color_t;

/**
 * @brief Predefined color palette for LED cycling
 * 
 * Array of RGB colors that the LED will cycle through
 */
static const rgb_color_t colors[] = {
    {255, 0, 0},     /**< Red */
    {0, 255, 0},     /**< Green */
    {0, 0, 255},     /**< Blue */
    {255, 255, 0},   /**< Yellow */
    {0, 255, 255},   /**< Cyan */
    {255, 0, 255},   /**< Magenta */
    {255, 255, 255}  /**< White */
};

/**
 * @brief Custom RMT encoder for WS2812B LED strip
 * 
 * This structure extends the base RMT encoder with WS2812B-specific
 * functionality including byte encoding and reset code generation.
 */
typedef struct {
    rmt_encoder_t base;              /**< Base RMT encoder */
    rmt_encoder_t *bytes_encoder;    /**< Encoder for RGB byte data */
    rmt_encoder_t *copy_encoder;     /**< Encoder for reset code */
    int state;                       /**< Current encoding state */
    rmt_symbol_word_t reset_code;    /**< WS2812B reset timing symbol */
} rmt_led_strip_encoder_t;

/**
 * @brief Encode RGB data into RMT symbols for WS2812B protocol
 * 
 * @param encoder Pointer to the encoder instance
 * @param channel RMT channel handle
 * @param primary_data Pointer to RGB data buffer
 * @param data_size Size of data buffer in bytes
 * @param ret_state Pointer to return encoding state
 * @return Number of RMT symbols encoded
 */
static size_t rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                     const void *primary_data, size_t data_size,
                                     rmt_encode_state_t *ret_state)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encode_state_t session_state = (rmt_encode_state_t)0;
    int state = 0;
    size_t encoded_symbols = 0;
    
    switch (led_encoder->state) {
    case 0: // send RGB data
        encoded_symbols += led_encoder->bytes_encoder->encode(led_encoder->bytes_encoder, channel,
                                                               primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 1; // switch to next state when current encoding session finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space for encoding artifacts
        }
    // fall-through
    case 1: // send reset code
        encoded_symbols += led_encoder->copy_encoder->encode(led_encoder->copy_encoder, channel,
                                                              &led_encoder->reset_code, sizeof(led_encoder->reset_code),
                                                              &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 0; // back to the initial encoding session
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space for encoding artifacts
        }
    }
out:
    *ret_state = (rmt_encode_state_t)state;
    return encoded_symbols;
}

/**
 * @brief Delete and cleanup LED strip encoder
 * 
 * @param encoder Pointer to the encoder to delete
 * @return ESP_OK on success
 */
static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    free(led_encoder);
    return ESP_OK;
}

/**
 * @brief Reset LED strip encoder to initial state
 * 
 * @param encoder Pointer to the encoder to reset
 * @return ESP_OK on success
 */
static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

/**
 * @brief Create a new LED strip encoder for WS2812B
 * 
 * Allocates and initializes a custom RMT encoder configured for
 * WS2812B timing requirements.
 * 
 * @param ret_encoder Pointer to store the created encoder handle
 * @return ESP_OK on success, ESP_ERR_NO_MEM if allocation fails
 */
static esp_err_t rmt_new_led_strip_encoder(rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    rmt_led_strip_encoder_t *led_encoder = NULL;
    rmt_copy_encoder_config_t copy_encoder_config = {};
    uint32_t reset_ticks = 40 * WS2812_RESET_US / 1000; // 280us
    
    led_encoder = (rmt_led_strip_encoder_t*)calloc(1, sizeof(rmt_led_strip_encoder_t));
    if (!led_encoder) {
        return ESP_ERR_NO_MEM;
    }
    led_encoder->base.encode = rmt_encode_led_strip;
    led_encoder->base.del = rmt_del_led_strip_encoder;
    led_encoder->base.reset = rmt_led_strip_encoder_reset;
    
    // WS2812B uses GRB format
    rmt_bytes_encoder_config_t bytes_encoder_config = {};
    bytes_encoder_config.bit0.level0 = 1;
    bytes_encoder_config.bit0.duration0 = 14;  // T0H=350ns (350ns / 25ns)
    bytes_encoder_config.bit0.level1 = 0;
    bytes_encoder_config.bit0.duration1 = 36;  // T0L=900ns (900ns / 25ns)
    bytes_encoder_config.bit1.level0 = 1;
    bytes_encoder_config.bit1.duration0 = 36;  // T1H=900ns
    bytes_encoder_config.bit1.level1 = 0;
    bytes_encoder_config.bit1.duration1 = 14;  // T1L=350ns
    bytes_encoder_config.flags.msb_first = 1;
    
    ret = rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder->bytes_encoder);
    if (ret != ESP_OK) {
        goto err;
    }
    
    ret = rmt_new_copy_encoder(&copy_encoder_config, &led_encoder->copy_encoder);
    if (ret != ESP_OK) {
        goto err;
    }
    
    led_encoder->reset_code.duration0 = reset_ticks;
    led_encoder->reset_code.level0 = 0;
    led_encoder->reset_code.duration1 = reset_ticks;
    led_encoder->reset_code.level1 = 0;
    
    *ret_encoder = &led_encoder->base;
    return ESP_OK;
    
err:
    if (led_encoder) {
        if (led_encoder->bytes_encoder) {
            rmt_del_encoder(led_encoder->bytes_encoder);
        }
        if (led_encoder->copy_encoder) {
            rmt_del_encoder(led_encoder->copy_encoder);
        }
        free(led_encoder);
    }
    return ret;
}

esp_err_t initRGBLed(void) {
    // Configure RMT TX channel
    rmt_tx_channel_config_t tx_chan_config = {};
    tx_chan_config.gpio_num = (gpio_num_t)LED_PIN;
    tx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_chan_config.resolution_hz = 40000000; // 40MHz, 1 tick = 25ns
    tx_chan_config.mem_block_symbols = 64;
    tx_chan_config.trans_queue_depth = 4;
    tx_chan_config.flags.invert_out = false;
    tx_chan_config.flags.with_dma = false;
    
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_channel));
    
    // Create LED strip encoder
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&led_encoder));
    
    // Enable RMT channel
    ESP_ERROR_CHECK(rmt_enable(led_channel));
    
    ESP_LOGI(TAG, "LED strip initialized on GPIO %d", LED_PIN);
    return ESP_OK;
}

/**
 * @brief Set RGB LED to specified color
 * 
 * @param red Red component (0-255)
 * @param green Green component (0-255)
 * @param blue Blue component (0-255)
 */
static void set_led_color(const uint8_t red, const uint8_t green, const uint8_t blue)
{
    // WS2812B uses GRB format
    uint8_t led_data[3] = {green, red, blue};
    
    rmt_transmit_config_t tx_config = {};
    tx_config.loop_count = 0;
    
    ESP_ERROR_CHECK(rmt_transmit(led_channel, led_encoder, led_data, sizeof(led_data), &tx_config));
    rmt_tx_wait_all_done(led_channel, portMAX_DELAY);
}

void vBlinkRGBTask(void *pvParameters) {
    const size_t numColors = sizeof(colors) / sizeof(colors[0]);
    size_t colorIndex = 0;
    
    // Suppress unused parameter warning
    (void)pvParameters;

    ESP_LOGI(TAG, "RGB LED Blink Task Started");

    while (1) {
        if (led_enabled) {
            // Turn on LED with current color
            set_led_color(colors[colorIndex].red, colors[colorIndex].green, colors[colorIndex].blue);
            ESP_LOGI(TAG, "Color: %zu (R:%d, G:%d, B:%d)", colorIndex, 
                     colors[colorIndex].red, colors[colorIndex].green, colors[colorIndex].blue);
            vTaskDelay(pdMS_TO_TICKS(500)); // On for 500ms

            // Turn off LED
            set_led_color(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(500)); // Off for 500ms

            // Move to next color
            colorIndex = (colorIndex + 1) % numColors;
        } else {
            // LED disabled - turn off and wait
            set_led_color(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100)); // Check status every 100ms
        }
    }
}

/**
 * @brief Toggle the LED enabled state
 */
void toggleLedEnabled(void) {
    led_enabled = !led_enabled;
}

/**
 * @brief Check if LED is enabled
 * 
 * @return true if LED blinking is enabled, false otherwise
 */
bool isLedEnabled(void) {
    return led_enabled;
}
