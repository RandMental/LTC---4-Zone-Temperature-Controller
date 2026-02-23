#pragma once`	0```	```````````````

/*
 * =============================================================================
 * config.h - Configuration file for Victron Temperature Monitor
 * =============================================================================
 *
 * Edit this file to configure WiFi, MQTT, pins, thresholds, and timing.
 * Keep sensitive credentials here and exclude from version control if needed.
 *
 * =============================================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

// ================================ DEBUG CONFIGURATION ================================
// Debug levels: 0=none, 1=error, 2=warn, 3=info, 4=debug
#define DEBUG_LEVEL 3

// ================================ DEVICE CONFIGURATION ================================
#define NUM_TEMP_SENSORS  4   // Number of DS18B20 temperature sensors
#define NUM_FANS          4   // Number of fan relays

// ================================ WIFI CONFIG ================================
#define WIFI_SSID               "IOT"
#define WIFI_PASSWORD           "7z14kpak31631"
#define WIFI_CONNECT_TIMEOUT_MS 10000
#define WIFI_RECONNECT_INTERVAL_MS 30000

// ================================ MQTT CONFIG ================================
#define MQTT_HOST           "192.168.40.250"
#define MQTT_PORT           1883
#define MQTT_USER           "labmqtt"
#define MQTT_PASSW          "7Z14kp"
#define MQTT_CLIENT_NAME    "VictronTempMonitor"

// MQTT Base Topic
#define MQTT_BASE_TOPIC     "solar"

// MQTT Topics - Monitor Status
#define MQTT_PUB_STATUS     "solar/monitor/status"
#define MQTT_PUB_WIFI_RSSI  "solar/monitor/rssi"
#define MQTT_PUB_WIFI_IP    "solar/monitor/ip"
#define MQTT_PUB_UPTIME     "solar/monitor/uptime"
#define MQTT_PUB_HEAP_FREE  "solar/monitor/heap_free"
#define MQTT_PUB_VERSION    "solar/monitor/version"

// MQTT Topics - Ambient (DHT22)
#define MQTT_PUB_AMB_TEMP   "solar/ambient/temperature"
#define MQTT_PUB_AMB_HUM    "solar/ambient/humidity"

// MQTT QoS and retain settings
#define MQTT_QOS            1
#define MQTT_RETAIN         true

// ================================ NTP CONFIG ================================
#define NTP_SERVER          "192.168.20.1"
#define GMT_OFFSET_SEC      7200        // GMT+2 (South Africa)
#define DAYLIGHT_OFFSET_SEC 0           // No DST

// ================================ PIN DEFINITIONS 
/* Hardware Connections - ESP32-S3-DevKitC-1 (44-pin)
 *
 * ESP32-S3 GPIO Safety:
 *   - Strapping pins (avoid): GPIO 0, 3, 45, 46
 *   - USB pins (avoid): GPIO 19, 20
 *   - JTAG pins (avoid): GPIO 39-42
 *   - UART0 (avoid): GPIO 43, 44
 *   - PSRAM (if equipped, avoid): GPIO 26-32
 *   - Safe GPIOs: 1-18, 21, 35-38, 47-48
 */

// Sensor pins
#define ONEWIRE_PIN         4     // DS18B20 bus (with 4.7k pull-up to 3.3V)
#define DHTPIN              5     // DHT22 (with 10k pull-up to 3.3V)

//  ================================
// ILI9342 3.2" (240x320) Display
#define TFT_WIDTH     240
#define TFT_HEIGHT    320
#define TFT_ROTATION  2   // Portrait, USB at bottom

// TFT Display pins (SPI2)
#define TFT_CS              10    // SPI2 default SS
#define TFT_DC              9
#define TFT_RST             8
#define TFT_MOSI            11    // SPI2 default MOSI
#define TFT_SCLK            12    // SPI2 default SCK
#define TFT_MISO            13    // SPI2 default MISO (not used by display)


// XPT2046 Touchscreen pin connections (shares SPI2 bus with display)
// T_CLK  -> GPIO12 (SPI2 SCK, shared with TFT)0...
// T_DIN  -> GPIO11 (SPI2 MOSI, shared with TFT)
// T_DO   -> GPIO13 (SPI2 MISO, required for touch readback)
#define TCH_CS              14    // Dedicated Touch CS (active low)
#define TCH_IRQ             7     // Touch interrupt (low when touched)

// SD Card (shares SPI2 bus with display and touchscreen)
#define SD_CS_PIN           6     // SD card chip select (active low)

// RGB LED (WS2812 NeoPixel on ESP32-S3-DevKitC-1)
#define RGB_LED_PIN         48    // Onboard RGB LED
#define RGB_BRIGHTNESS      25    // LED brightness (0-255), keep low to avoid glare
#define RGB_PULSE_MS        100   // How long the LED stays on during pulse

// Fan relay pins
// FAN_ACTIVE_LOW: true = LOW turns fan ON (inverted), false = HIGH turns fan ON (normal)
#define FAN_ACTIVE_LOW      true
// Unused sensors (no valid address) keep fan off
// Sensor errors on configured sensors force fan ON for safety
#define FAN_RELAY_0         15    // MPPT_W
#define FAN_RELAY_1         16    // MPPT_E
#define FAN_RELAY_2         17    // MultiPlus1
#define FAN_RELAY_3         18    // Batteries

// ================================ BUTTON TIMING ================================
#define BUTTON_DEBOUNCE_MS      100     // Minimum press to count as valid
#define SHORT_PRESS_MAX_MS      1000    // Under 1s = test fans or skip
#define SKIP_PRESS_MIN_MS       1000    // 1s+ in assignment mode = skip
#define LONG_PRESS_MS           5000    // 5+ seconds = enter assignment mode
#define TEST_FAN_DURATION_MS    5000    // How long fans run during test
#define ASSIGNMENT_TIMEOUT_MS   30000   // 30s timeout per step (reboot if no sensor)
#define CONFIRM_PRESS_MIN_MS    200     // Minimum button press for confirmation
#define CONFIRM_RELEASE_DELAY_MS 1000   // Wait 1s after button release before advancing
#define STARTUP_SCREEN_MS       500     // Brief startup display

// ================================ TEMPERATURE THRESHOLDS ================================
// Per-device thresholds: { MPPT_W, MPPT_E, MultiPlus_1, Batteries }
#define TEMP_FAN_OFF_MPPT_W     35.0f
#define TEMP_FAN_OFF_MPPT_E     35.0f
#define TEMP_FAN_OFF_MULTIPLUS  35.0f
#define TEMP_FAN_OFF_BATTERIES  30.0f

#define TEMP_FAN_ON_MPPT_W      35.0f
#define TEMP_FAN_ON_MPPT_E      35.0f
#define TEMP_FAN_ON_MULTIPLUS   35.0f
#define TEMP_FAN_ON_BATTERIES   30.0f

#define TEMP_FAN_HYSTERESIS     5.0f    // Turn off fan when temp drops this much below FAN_ON
#define TEMP_MIN_VALID          -5.0f   // Minimum valid DS18B20 reading
#define TEMP_MAX_VALID          75.0f   // Maximum valid DS18B20 reading

// ================================ TIMING INTERVALS ================================
#define SENSOR_INTERVAL     2000    // 2s - read sensors
#define DISP_INTERVAL       4000    // 4s - cycle display screens
#define MQTT_INTERVAL       30000   // 30s - publish sensor readings
#define STATUS_INTERVAL     60000   // 1min - publish status/heap
#define NTP_INTERVAL        7200000 // 2h - sync NTP time
#define WDT_TIMEOUT_S       60      // Watchdog timeout (seconds)

// ================================ SENSOR FILTERING ================================
#define FILTER_SAMPLES          5   // Moving average window size
#define SENSOR_ERROR_THRESHOLD  3   // Consecutive errors before marking sensor failed

// ================================ STARTUP SCREEN ================================
// Startup screen title (3 lines, centered)
#define STARTUP_LINE_1      "Labfin"
#define STARTUP_LINE_2      "Temperature"
#define STARTUP_LINE_3      "Controller"

// ================================ DEVICE NAMES ================================
// Device names for display (max 10 chars)
#define DEVICE_NAME_0       "MPPT_Wes"
#define DEVICE_NAME_1       "MPPT_Oos"
#define DEVICE_NAME_2       "Inverter"
#define DEVICE_NAME_3       "Batterye"

// MQTT topic suffixes for each device
#define DEVICE_TOPIC_0      "mppt_w"
#define DEVICE_TOPIC_1      "mppt_e"
#define DEVICE_TOPIC_2      "inverter"
#define DEVICE_TOPIC_3      "batteries"



// ================================ DS18B20 SENSOR ADDRESSES ================================
// 1-Wire addresses for each temperature sensor (8 bytes each)
// Use sensor assignment mode (long press button >5s) to discover addresses
// Addresses starting with 0x28,0x00,0x00 are placeholders (sensor not configured)
//
// After assignment, addresses are printed to Serial in this format - copy them here:
#define SENSOR_ADDR_0  { 0x28, 0xD6, 0x0D, 0x95, 0xF0, 0x01, 0x3C, 0xB0 }  
#define SENSOR_ADDR_1  { 0x28, 0x79, 0xEC, 0x95, 0xF0, 0xFF, 0x3C, 0x9A }  
#define SENSOR_ADDR_2  { 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }  
#define SENSOR_ADDR_3  { 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }  

// ======================================================
// Fixed layout values for 240x320 display
// ======================================================
#define TITLE_Y           12
#define CONTENT_Y         48
#define LINE_SPACING      30
#define WIFI_ICON_SCALE   2
#define STATUS_ICON_SCALE 2

#endif // CONFIG_H
