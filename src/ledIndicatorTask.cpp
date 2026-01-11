#include "ledIndicatorTask.h"
#include "hardware_config.h"
#include "wifiManager.h"
#include "ntripClientTask.h"
#include "gnssReceiverTask.h"
#include "mqttClientTask.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "LEDTask";

// Task configuration
#define LED_TASK_STACK_SIZE     3072
#define LED_TASK_PRIORITY       2
#define LED_UPDATE_RATE_MS      100
#define LED_BLINK_PERIOD_MS     500
#define ACTIVITY_TIMEOUT_SEC    2

// LED state enumeration
typedef enum {
    LED_OFF = 0,
    LED_ON = 1,
    LED_BLINK = 2
} led_state_t;

// RGB color structure
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_color_t;

// Predefined colors for Neopixel status
static const rgb_color_t RGB_OFF = {0, 0, 0};
static const rgb_color_t RGB_GREEN = {0, 255, 0};
static const rgb_color_t RGB_YELLOW = {255, 255, 0};
static const rgb_color_t RGB_RED = {255, 0, 0};
static const rgb_color_t RGB_BLUE = {0, 0, 255};

// LED status structure
typedef struct {
    bool wifi_sta_connected;
    bool ntrip_connected;
    bool ntrip_data_activity;
    bool mqtt_connected;
    bool mqtt_activity;
    uint8_t gps_fix_quality;
    bool gps_data_valid;
    time_t last_ntrip_data_time;
    time_t last_mqtt_activity_time;
} led_status_t;

// Global variables
static TaskHandle_t led_task_handle = NULL;
static time_t last_ntrip_activity = 0;
static time_t last_mqtt_activity = 0;

// Update activity timestamps (called by other tasks)
void led_update_ntrip_activity(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    last_ntrip_activity = tv.tv_sec;
}

void led_update_mqtt_activity(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    last_mqtt_activity = tv.tv_sec;
}

// Initialize GPIO pins for discrete LEDs
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

// Initialize Neopixel RGB LED (placeholder - requires led_strip component)
static void init_neopixel_led(void) {
    // Configure STATUS_LED_PIN as output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << STATUS_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    gpio_config(&io_conf);
    gpio_set_level(STATUS_LED_PIN, 0); // Turn off initially
    
    ESP_LOGI(TAG, "Neopixel GPIO initialized (WS2812 control pending)");
}

// Update Neopixel with RGB color (placeholder)
static void update_neopixel(rgb_color_t color) {
    // TODO: Implement WS2812 control via RMT when led_strip component is available
    // For now, just use the pin as a simple on/off indicator
    if (color.r > 0 || color.g > 0 || color.b > 0) {
        gpio_set_level(STATUS_LED_PIN, 1); // On
    } else {
        gpio_set_level(STATUS_LED_PIN, 0); // Off
    }
}

// Calculate NTRIP LED state
static bool calculate_ntrip_led_state(const led_status_t *status, bool blink_state) {
    if (!status->ntrip_connected) {
        return false; // OFF
    } else if (status->ntrip_data_activity) {
        return blink_state; // BLINK
    } else {
        return true; // ON
    }
}

// Calculate MQTT LED state
static bool calculate_mqtt_led_state(const led_status_t *status, bool blink_state) {
    if (!status->mqtt_connected) {
        return false; // OFF
    } else if (status->mqtt_activity) {
        return blink_state; // BLINK
    } else {
        return true; // ON
    }
}

// Calculate RTK Float LED state
static bool calculate_rtk_float_led_state(const led_status_t *status, bool blink_state) {
    if (status->gps_fix_quality == GPS_FIX_DGPS) {
        return blink_state; // BLINK for RTK Float
    } else if (status->gps_fix_quality >= GPS_FIX_RTK_FIXED) {
        return true; // ON for RTK Fixed
    } else {
        return false; // OFF
    }
}

// Calculate system status color for Neopixel
static rgb_color_t calculate_system_status_color(const led_status_t *status) {
    // RED: Critical error (no WiFi STA and no valid GPS data)
    if (!status->wifi_sta_connected && !status->gps_data_valid) {
        return RGB_RED;
    }
    
    // YELLOW: Partial operation (WiFi connected but services not running optimally)
    if (!status->wifi_sta_connected || (!status->ntrip_connected && !status->gps_data_valid)) {
        return RGB_YELLOW;
    }
    
    // GREEN: Full operation (WiFi connected, GPS valid, NTRIP connected)
    if (status->wifi_sta_connected && status->gps_data_valid && status->ntrip_connected) {
        return RGB_GREEN;
    }
    
    // YELLOW: Default partial operation state
    return RGB_YELLOW;
}

// LED Indicator Task
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
    
    ESP_LOGI(TAG, "LED Indicator Task started");
    
    // Initialize LEDs
    init_led_gpios();
    init_neopixel_led();
    
    while (1) {
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
        rgb_color_t rgb = calculate_system_status_color(&status);
        update_neopixel(rgb);
        
        // Delay for update rate
        vTaskDelay(pdMS_TO_TICKS(LED_UPDATE_RATE_MS));
    }
}

// Public API Implementation

void led_indicator_task_init(void) {
    // Create task
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
