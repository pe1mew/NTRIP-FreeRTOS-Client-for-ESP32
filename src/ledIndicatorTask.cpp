/**
 * @file ledIndicatorTask.cpp
 * @brief Implementation of the LED indicator task for system status visualization on ESP32.
 *
 * This file contains the logic for managing discrete and RGB LEDs to reflect WiFi, NTRIP, MQTT, and GNSS/RTK status.
 */

#include "ledIndicatorTask.h"
#include "hardware_config.h"
#include "wifiManager.h"
#include "ntripClientTask.h"
#include "gnssReceiverTask.h"
#include "mqttClientTask.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/rmt_tx.h>
#include <esp_log.h>
#include <string.h>
#include <sys/time.h>
#include <freertos/queue.h>

/**
 * @brief Tag for ESP-IDF logging.
 */
static const char *TAG = "LEDTask";

/**
 * @def LED_TASK_STACK_SIZE
 * @brief Stack size for the LED indicator task.
 */
#define LED_TASK_STACK_SIZE     3072
/**
 * @def LED_TASK_PRIORITY
 * @brief Task priority for the LED indicator task.
 */
#define LED_TASK_PRIORITY       2
/**
 * @def LED_UPDATE_RATE_MS
 * @brief LED update interval in milliseconds.
 */
#define LED_UPDATE_RATE_MS      100
/**
 * @def LED_BLINK_PERIOD_MS
 * @brief LED blink period in milliseconds.
 */
#define LED_BLINK_PERIOD_MS     500
/**
 * @def ACTIVITY_TIMEOUT_SEC
 * @brief Timeout in seconds to consider activity as stale.
 */
#define ACTIVITY_TIMEOUT_SEC    2
/**
 * @def NUM_LEDS
 * @brief Number of LEDs in the strip.
 */
#define NUM_LEDS        1     // Number of LEDs in the strip

/**
 * @brief LED state enumeration for discrete LEDs.
 */
typedef enum {
    LED_OFF = 0,   /**< LED is off */
    LED_ON = 1,    /**< LED is on */
    LED_BLINK = 2  /**< LED is blinking */
} led_state_t;

/**
 * @brief RGB color structure for Neopixel LED.
 */
typedef struct {
    uint8_t r; /**< Red component */
    uint8_t g; /**< Green component */
    uint8_t b; /**< Blue component */
} rgb_color_t;

/**
 * @brief Predefined colors for Neopixel status indication.
 */
static const rgb_color_t RGB_OFF = {0, 0, 0};
static const rgb_color_t RGB_GREEN = {0, 255, 0};
static const rgb_color_t RGB_YELLOW = {255, 255, 0};
static const rgb_color_t RGB_RED = {255, 0, 0};
static const rgb_color_t RGB_BLUE = {0, 0, 255};

/**
 * @brief Structure holding the current status for all LED indicators.
 */
typedef struct {
    bool wifi_sta_connected;        /**< WiFi STA connection status */
    bool ntrip_connected;           /**< NTRIP client connection status */
    bool ntrip_data_activity;       /**< Recent NTRIP data activity */
    bool mqtt_connected;            /**< MQTT client connection status */
    bool mqtt_activity;             /**< Recent MQTT data activity */
    uint8_t gps_fix_quality;        /**< GNSS fix quality (NMEA GGA field 6) */
    bool gps_data_valid;            /**< GNSS data validity */
    time_t last_ntrip_data_time;    /**< Last NTRIP data timestamp */
    time_t last_mqtt_activity_time; /**< Last MQTT activity timestamp */
} led_status_t;

// RGB LED command structure
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    TickType_t duration_ticks; // 0 = persistent, >0 = temporary
} rgb_led_cmd_t;

static TaskHandle_t led_task_handle = NULL;
static time_t last_ntrip_activity = 0;
static time_t last_mqtt_activity = 0;
static rmt_channel_handle_t led_channel = NULL;
static rmt_encoder_handle_t led_encoder = NULL;
static QueueHandle_t rgb_led_cmd_queue = NULL;

/**
 * @brief Update NTRIP activity timestamp.
 *
 * Call this from the NTRIP client task when new data is received to indicate activity.
 */
void led_update_ntrip_activity(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    last_ntrip_activity = tv.tv_sec;
}

/**
 * @brief Update MQTT activity timestamp.
 *
 * Call this from the MQTT client task when new data is received to indicate activity.
 */
void led_update_mqtt_activity(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    last_mqtt_activity = tv.tv_sec;
}

/**
 * @brief Initialize GPIO pins for discrete status LEDs.
 */
static void init_led_gpios(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << WIFI_LED) | 
                       (1ULL << NTRIP_LED) | 
                       (1ULL << MQTT_LED) | 
                       (1ULL << FIX_RTK_LED) | 
                       (1ULL << FIX_RTKFLOAT_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LED GPIOs: %s", esp_err_to_name(err));
        return;
    }
    
    // Initialize all LEDs to OFF
    gpio_set_level(WIFI_LED, 0);
    gpio_set_level(NTRIP_LED, 0);
    gpio_set_level(MQTT_LED, 0);
    gpio_set_level(FIX_RTK_LED, 0);
    gpio_set_level(FIX_RTKFLOAT_LED, 0);
    
    ESP_LOGI(TAG, "Discrete LEDs initialized");
}

/**
 * @brief Initialize Neopixel RGB LED using RMT driver.
 */
esp_err_t initRGBLed(void) {
    rmt_tx_channel_config_t tx_chan_config = {};
    tx_chan_config.gpio_num = (gpio_num_t)STATUS_LED_PIN;
    tx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_chan_config.resolution_hz = 40000000; // 40MHz, 1 tick = 25ns
    tx_chan_config.mem_block_symbols = 64;
    tx_chan_config.trans_queue_depth = 4;
    tx_chan_config.flags.invert_out = false;
    tx_chan_config.flags.with_dma = false;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_channel));
    // WS2812B uses GRB format
    rmt_bytes_encoder_config_t bytes_encoder_config = {};
    bytes_encoder_config.bit0.level0 = 1;
    bytes_encoder_config.bit0.duration0 = 14;
    bytes_encoder_config.bit0.level1 = 0;
    bytes_encoder_config.bit0.duration1 = 36;
    bytes_encoder_config.bit1.level0 = 1;
    bytes_encoder_config.bit1.duration0 = 36;
    bytes_encoder_config.bit1.level1 = 0;
    bytes_encoder_config.bit1.duration1 = 14;
    bytes_encoder_config.flags.msb_first = 1;
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder));
    ESP_ERROR_CHECK(rmt_enable(led_channel));
    ESP_LOGI(TAG, "LED strip initialized on GPIO %d", STATUS_LED_PIN);
    return ESP_OK;
}

/**
 * @brief Set the color of the RGB LED.
 *
 * @param red Red component (0-255).
 * @param green Green component (0-255).
 * @param blue Blue component (0-255).
 */
void set_led_color(uint8_t red, uint8_t green, uint8_t blue) {
    uint8_t led_data[3] = {green, red, blue};
    rmt_transmit_config_t tx_config = {};
    tx_config.loop_count = 0;
    ESP_ERROR_CHECK(rmt_transmit(led_channel, led_encoder, led_data, sizeof(led_data), &tx_config));
    rmt_tx_wait_all_done(led_channel, portMAX_DELAY);
}

/**
 * @brief Calculate NTRIP LED state.
 *
 * @param status Pointer to current LED status structure.
 * @param blink_state Current blink state (true/false).
 * @return true if LED should be ON, false otherwise.
 */
static bool calculate_ntrip_led_state(const led_status_t *status, bool blink_state) {
    if (!status->ntrip_connected) {
        return false; // OFF
    } else if (status->ntrip_data_activity) {
        return blink_state; // BLINK
    } else {
        return true; // ON
    }
}

/**
 * @brief Calculate MQTT LED state.
 *
 * @param status Pointer to current LED status structure.
 * @param blink_state Current blink state (true/false).
 * @return true if LED should be ON, false otherwise.
 */
static bool calculate_mqtt_led_state(const led_status_t *status, bool blink_state) {
    if (!status->mqtt_connected) {
        return false; // OFF
    } else if (status->mqtt_activity) {
        return blink_state; // BLINK
    } else {
        return true; // ON
    }
}

/**
 * @brief Calculate RTK Float LED state.
 *
 * @param status Pointer to current LED status structure.
 * @param blink_state Current blink state (true/false).
 * @return true if LED should be ON, false otherwise.
 *
 * - RTK Float: LED blinks
 * - RTK Fixed: LED ON
 * - Otherwise: LED OFF
 */
static bool calculate_rtk_float_led_state(const led_status_t *status, bool blink_state) {
    if (status->gps_fix_quality == GPS_FIX_RTK_FLOAT) {
        return blink_state; // BLINK for RTK Float
    } else if (status->gps_fix_quality == GPS_FIX_RTK_FIXED) {
        return true; // ON for RTK Fixed
    } else {
        return false; // OFF
    }
}

/**
 * @brief LED Indicator Task main loop.
 *
 * This FreeRTOS task manages all status LEDs and updates them based on system state.
 *
 * @param pvParameters Unused.
 */
static void led_indicator_task(void *pvParameters) {
    led_status_t status = {
        .wifi_sta_connected = false,
        .ntrip_connected = false,
        .ntrip_data_activity = false,
        .mqtt_connected = false,
        .mqtt_activity = false,
        .gps_fix_quality = 0,
        .gps_data_valid = false,
        .last_ntrip_data_time = 0,
        .last_mqtt_activity_time = 0
    };
    uint32_t blink_counter = 0;
    bool blink_state = false;
    rgb_color_t override_rgb = {0, 0, 0};
    TickType_t override_until = 0;
    
    ESP_LOGI(TAG, "LED Indicator Task started");
    
    // Initialize LEDs
    init_led_gpios();
    initRGBLed();
    // Set RGB LED to off at startup
    set_led_color(0, 0, 0);
    
    while (1) {
        // Check for RGB LED command
        rgb_led_cmd_t cmd;
        if (rgb_led_cmd_queue && xQueueReceive(rgb_led_cmd_queue, &cmd, 0)) {
            override_rgb.r = cmd.r;
            override_rgb.g = cmd.g;
            override_rgb.b = cmd.b;
            if (cmd.duration_ticks > 0) {
                override_until = xTaskGetTickCount() + cmd.duration_ticks;
            } else {
                override_until = 0xFFFFFFFF;
            }
            set_led_color(override_rgb.r, override_rgb.g, override_rgb.b);
        }
        
        // Update blink counter (toggles every 500ms for 1 Hz blink)
        blink_counter++;
        if (blink_counter >= (LED_BLINK_PERIOD_MS / LED_UPDATE_RATE_MS)) {
            blink_state = !blink_state;
            blink_counter = 0;
        }
        
        // Collect status from all subsystems
        status.wifi_sta_connected = wifi_manager_is_sta_connected();
        status.ntrip_connected = ntrip_client_is_connected();
        
        // Get GNSS data for fix quality
        gnss_data_t gnss_data;
        gnss_get_data(&gnss_data);
        status.gps_data_valid = gnss_data.valid;
        status.gps_fix_quality = gnss_data.fix_quality; // Use centrally parsed value
        
        // Check activity timestamps
        struct timeval tv;
        gettimeofday(&tv, NULL);
        time_t now = tv.tv_sec;
        
        status.last_ntrip_data_time = last_ntrip_activity;
        status.last_mqtt_activity_time = last_mqtt_activity;
        status.ntrip_data_activity = (now - status.last_ntrip_data_time) < ACTIVITY_TIMEOUT_SEC;
        status.mqtt_activity = (now - status.last_mqtt_activity_time) < ACTIVITY_TIMEOUT_SEC;
        
        // MQTT connection status
        status.mqtt_connected = mqtt_is_connected();
        
        // Update discrete LEDs
        gpio_set_level(WIFI_LED, status.wifi_sta_connected ? 1 : 0);
        gpio_set_level(NTRIP_LED, calculate_ntrip_led_state(&status, blink_state) ? 1 : 0);
        gpio_set_level(MQTT_LED, calculate_mqtt_led_state(&status, blink_state) ? 1 : 0);
        gpio_set_level(FIX_RTK_LED, (status.gps_data_valid && status.gps_fix_quality >= GPS_FIX_GPS) ? 1 : 0);
        gpio_set_level(FIX_RTKFLOAT_LED, calculate_rtk_float_led_state(&status, blink_state) ? 1 : 0);
        
        // Update Neopixel RGB LED
        if (override_until && xTaskGetTickCount() < override_until) {
            set_led_color(override_rgb.r, override_rgb.g, override_rgb.b);
        } else {
            override_until = 0;
            // set_led_color or other logic can be placed here if needed
        }
        
        // Delay for update rate
        vTaskDelay(pdMS_TO_TICKS(LED_UPDATE_RATE_MS));
    }
}

/**
 * @brief Initialize and start the LED Indicator Task.
 *
 * This function creates the FreeRTOS task that manages all status LEDs.
 */
void led_indicator_task_init(void) {
    // Create RGB LED command queue
    rgb_led_cmd_queue = xQueueCreate(4, sizeof(rgb_led_cmd_t));
    // Create task
    initRGBLed(); // Initialize RGB LED RMT driver
    set_led_color(0, 0, 0); // Set RGB LED to off at initialization
    BaseType_t result = xTaskCreate(
        led_indicator_task,
        "led_indicator",
        LED_TASK_STACK_SIZE,
        NULL,
        LED_TASK_PRIORITY,
        &led_task_handle
    );
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED Indicator Task");
    }
}

// API for other tasks to send RGB LED command
void led_set_rgb(uint8_t r, uint8_t g, uint8_t b, TickType_t duration_ticks) {
    if (rgb_led_cmd_queue) {
        rgb_led_cmd_t cmd = { r, g, b, duration_ticks };
        xQueueSend(rgb_led_cmd_queue, &cmd, 0);
    }
}

/**
 * @brief Stop the LED Indicator Task and turn off all LEDs.
 */
void led_indicator_task_stop(void) {
    if (led_task_handle != NULL) {
        vTaskDelete(led_task_handle);
        led_task_handle = NULL;
        ESP_LOGI(TAG, "LED Indicator Task stopped");
    }
    
    // Turn off all LEDs
    gpio_set_level(WIFI_LED, 0);
    gpio_set_level(NTRIP_LED, 0);
    gpio_set_level(MQTT_LED, 0);
    gpio_set_level(FIX_RTK_LED, 0);
    gpio_set_level(FIX_RTKFLOAT_LED, 0);
    gpio_set_level(STATUS_LED_PIN, 0);
}
