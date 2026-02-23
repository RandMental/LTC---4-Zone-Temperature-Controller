/*
 * =============================================================================
 * Victron Temperature Monitor - 4-Channel Production Version
 * =============================================================================
 *
 * ESP32-S3-based temperature monitoring system for Victron solar equipment.
 * Monitors 4x DS18B20 sensors, controls 4x cooling fans, publishes to MQTT,
 * and displays status on ST7735 TFT.
 *
 * Original project: https://RandomNerdTutorials.com/esp32-mqtt-publish-ds18b20-temperature-arduino/
 *
 * History:
 *   DdR - 18-03-2024  Initial version with two DS1820 sensors
 *   DdR - 30-03-2024  Added ST7735 TFT display
 *   DdR - 26-01-2026  Added relay fan control, updated libraries
 *   DdR - 27-01-2026  Switched to ESP32MQTTClient library
 *   DdR - 27-01-2026  Production hardening (watchdog, filtering, reconnection)
 *   DdR - 27-01-2026  Upgraded to 4 sensors and 4 fans
 *   DdR - 28-01-2026  Added sensor address assignment mode
 *   DdR - 28-01-2026  GPIO 15->14 for OneWire (strapping pin fix), watchdog init fix
 *   DdR - 29-01-2026  Migrated to ESP32-S3-DevKitC-1 (44-pin), GPIO remapping
 *   DdR - 30-01-2026  Fixed: Capacitive touch on GPIO6, explicit SPI init for ST7735
 *   DdR - 05-02-2026  Update code for ILI9341 240x320 3.2" display
 *
 * Features:
 *   - 4x DS18B20 temperature sensors with moving average filter
 *   - 4x Fan relay outputs with hysteresis control
 *   - DHT22 ambient temperature/humidity sensor
 *   - ST7735 1.8" 128x160 TFT display with status bar
 *   - MQTT publishing with QoS 1 and Last Will Testament
 *   - Hardware watchdog timer (30s timeout)
 *   - Non-blocking design throughout
 *   - Exponential backoff for WiFi reconnection
 *   - Graceful degradation on sensor failures
 *   - Persistent boot counter
 *   - Heap monitoring for memory leak detection
 *   - Button-activated sensor address assignment mode
 *   - NVS persistence for sensor addresses
 *
 * =============================================================================
 */

// ================================ INCLUDES ================================
#include "config.h"
#include "icons.h"
#include <Arduino.h>
#include <stdarg.h>
#include <string>
#include <WiFi.h>
#include <ESP32MQTTClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <time.h>
#include <DHT.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <SD.h>

// ================================ VERSION INFO ================================
#define FIRMWARE_VERSION "2.2.2"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__
#define DISPLAY_SCALE 1


// ================================ DEBUG CONFIGURATION ================================
// Debug level is defined in config.h

#if DEBUG_LEVEL >= 1
#define LOG_ERROR(fmt, ...) logMessage("ERROR", fmt, ##__VA_ARGS__)
#else
#define LOG_ERROR(fmt, ...)
#endif

#if DEBUG_LEVEL >= 2
#define LOG_WARN(fmt, ...) logMessage("WARN", fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(fmt, ...)
#endif

#if DEBUG_LEVEL >= 3
#define LOG_INFO(fmt, ...) logMessage("INFO", fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)
#endif

#if DEBUG_LEVEL >= 4
#define LOG_DEBUG(fmt, ...) logMessage("DEBUG", fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)
#endif

// ================================ FONTS ================================
#define font_small &FreeMonoBold12pt7b
#define font_large &FreeMonoBold24pt7b

// =============================================================================
// Layout for 240x320 ILI9342 display (scaled 2x from 128x160 original)
// All Y values are baselines for text positioning with custom fonts
// =============================================================================

// Status bar (top row: WiFi icon, IP address, SD icon, MQTT)
#define STATUSBAR_Y 0
#define STATUSBAR_HEIGHT 30
#define WIFI_ICON_X 6
#define WIFI_ICON_Y 1
#define IP_TEXT_X 44
#define IP_TEXT_Y 11
#define SD_ICON_X 162
#define SD_ICON_Y 7

// Time display (large centered time)
#define TIME_ROW_Y 30
#define TIME_ROW_HEIGHT 60
#define TIME_BASELINE_Y 80  // Baseline for 24pt font

// Date display (smaller, below time)
#define DATE_ROW_Y 90
#define DATE_ROW_HEIGHT 40
#define DATE_BASELINE_Y 120  // Baseline for 12pt font

// Device name row (name + fan icon inline)
#define NAME_ROW_Y 140
#define NAME_ROW_HEIGHT 50
#define NAME_BASELINE_Y 170  // Baseline for 12pt font

// Temperature row (icon + large temp value)
#define TEMP_ROW_Y 160
#define TEMP_ROW_HEIGHT 80
#define TEMP_BASELINE_Y 230  // Baseline for 24pt font

// Humidity row (ambient screen only)
#define HUM_ROW_Y 240
#define HUM_ROW_HEIGHT 80
#define HUM_BASELINE_Y 300  // Baseline for 24pt font

// Test/Assignment mode screens
#define TITLE_Y 30
#define TITLE_LINE_Y 50
#define PROGRESS_Y 80
#define MODE_NAME_Y 140
#define STATUS_Y 200
#define BOX_Y 260
#define BOX_SIZE 40
#define BOX_SPACING 50

// Icon dimensions defined in icons.h

// Fan relay pins array (uses defines from config.h)
const uint8_t FAN_RELAY_PINS[NUM_FANS] = { FAN_RELAY_0, FAN_RELAY_1, FAN_RELAY_2, FAN_RELAY_3 };

// Note: Fan output states (fanOnState/fanOffState) are set at runtime from cfg.fanActiveLow
// Note: Device names, topics, thresholds, and NTP config moved to RuntimeConfig (cfg.*)

// ================================ SENSOR ADDRESSES ================================
/*
 * DS18B20 1-Wire sensor addresses.
 * Loaded from config.h. Use assignment mode to discover new addresses.
 * New addresses are printed to Serial - copy them to config.h.
 *
 * NOTE: Sensors with address starting with 0x28,0x00,0x00 are placeholders.
 */
DeviceAddress sensorAddresses[NUM_TEMP_SENSORS] = {
  SENSOR_ADDR_0,  // Sensor 0: MPPT_W
  SENSOR_ADDR_1,  // Sensor 1: MPPT_E
  SENSOR_ADDR_2,  // Sensor 2: MultiPlus_1
  SENSOR_ADDR_3   // Sensor 3: Batteries
};

// ================================ GLOBAL OBJECTS ================================
OneWire oneWire(ONEWIRE_PIN);
DallasTemperature sensors(&oneWire);
DHT dht(DHTPIN, DHT22);
// ILI9342 3.2" 240x320 display (hardware SPI)
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
// XPT2046 touchscreen (shares SPI bus, separate CS)
XPT2046_Touchscreen touch(TCH_CS, TCH_IRQ);

// Color definitions (use ST77XX for compatibility with both displays)
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_BLUE 0x001F
#define COLOR_CYAN 0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_YELLOW 0xFFE0
#define COLOR_ORANGE 0xFD20

ESP32MQTTClient mqttClient;
Preferences preferences;

// ================================ SENSOR DATA STRUCTURE ================================
/**
 * @brief Stores sensor data with moving average filtering
 *
 * Provides noise rejection through averaging and error detection
 * through consecutive error counting.
 */
struct SensorData {
  float values[FILTER_SAMPLES];  // Circular buffer for readings
  uint8_t index;                 // Current position in buffer
  uint8_t validCount;            // Number of valid readings in buffer
  uint8_t errorCount;            // Consecutive error count
  float filtered;                // Filtered (averaged) value
  bool valid;                    // True if sensor is working
  bool enabled;                  // True if sensor is configured

  void init() {
    for (int i = 0; i < FILTER_SAMPLES; i++) values[i] = 0;
    index = 0;
    validCount = 0;
    errorCount = 0;
    filtered = 0;
    valid = false;
    enabled = true;
  }

  /**
   * @brief Add a new reading to the filter
   * @param value Temperature reading (or invalid marker)
   */
  void addReading(float value) {
    if (value >= TEMP_MIN_VALID && value <= TEMP_MAX_VALID) {
      values[index] = value;
      index = (index + 1) % FILTER_SAMPLES;
      if (validCount < FILTER_SAMPLES) validCount++;
      errorCount = 0;
      valid = true;

      // Calculate moving average
      float sum = 0;
      for (uint8_t i = 0; i < validCount; i++) sum += values[i];
      filtered = sum / validCount;
    } else {
      errorCount++;
      if (errorCount >= SENSOR_ERROR_THRESHOLD) {
        valid = false;
      }
    }
  }
};

// ================================ RUNTIME CONFIGURATION ================================
/**
 * @brief Runtime configuration loaded from SD card INI file
 *
 * All fields are initialized from compiled #define defaults.
 * If an SD card with /config.ini is present, values are overridden at boot.
 */
struct RuntimeConfig {
  // WiFi
  char wifiSsid[33];
  char wifiPassword[65];
  uint32_t wifiConnectTimeoutMs;
  uint32_t wifiReconnectIntervalMs;
  // MQTT
  char mqttHost[65];
  uint16_t mqttPort;
  char mqttUser[33];
  char mqttPassword[33];
  char mqttClientName[33];
  char mqttBaseTopic[33];
  uint8_t mqttQos;
  bool mqttRetain;
  // MQTT topic strings (built at runtime from mqttBaseTopic)
  char mqttPubStatus[64];
  char mqttPubWifiRssi[64];
  char mqttPubWifiIp[64];
  char mqttPubUptime[64];
  char mqttPubHeapFree[64];
  char mqttPubVersion[64];
  char mqttPubAmbTemp[64];
  char mqttPubAmbHum[64];
  // NTP
  char ntpServer[65];
  long gmtOffsetSec;
  int daylightOffsetSec;
  // Devices (4 sensors)
  char deviceNames[4][17];
  char deviceTopics[4][17];
  // Temperature thresholds
  float tempFanOn[4];
  float tempFanOff[4];
  float tempFanHysteresis;
  float tempMinValid;
  float tempMaxValid;
  // Timing
  uint32_t sensorInterval;
  uint32_t displayInterval;
  uint32_t mqttInterval;
  uint32_t statusInterval;
  uint32_t ntpInterval;
  // Sensor addresses
  uint8_t sensorAddresses[4][8];
  // Display
  uint8_t tftRotation;
  char startupLine1[21];
  char startupLine2[21];
  char startupLine3[21];
  // Fan
  bool fanActiveLow;
  // Watchdog
  uint32_t wdtTimeoutS;
  // SD status
  bool sdCardPresent;
  bool configLoaded;
};

RuntimeConfig cfg;    // Global runtime config
uint8_t fanOnState;   // Runtime: LOW or HIGH (replaces fanOnState #define)
uint8_t fanOffState;  // Runtime: HIGH or LOW (replaces fanOffState #define)

// ================================ OPERATING MODES ================================
enum OperatingMode {
  MODE_STARTUP,           // Initial boot screen
  MODE_NORMAL,            // Standard operation
  MODE_TEST_FANS,         // Testing all fans
  MODE_SENSOR_CLEAR_BUS,  // Waiting for sensors to be removed
  MODE_SENSOR_ASSIGNMENT  // Assigning sensor addresses one by one
};

// ================================ SYSTEM STATE ================================
/**
 * @brief Central state structure for the entire system
 *
 * Consolidates all state variables for easy initialization and access.
 */
struct SystemState {
  // Connection state
  bool wifiConnected;
  bool mqttConnected;
  int wifiRSSI;
  uint32_t wifiReconnectAttempts;

  // Timing (using uint32_t for millis() values)
  uint32_t lastMQTTPublish;
  uint32_t lastDisplayUpdate;
  uint32_t lastNTPSync;
  uint32_t lastSensorRead;
  uint32_t lastStatusPublish;
  uint32_t lastWifiCheck;
  uint32_t bootTime;

  // Display
  uint8_t displayScreen;

  // Sensors and fans
  SensorData tempSensors[NUM_TEMP_SENSORS];
  bool fanStates[NUM_FANS];
  SensorData ambientTemp;
  SensorData ambientHum;

  // Statistics
  uint32_t bootCount;
  uint32_t publishCount;
  uint32_t sensorErrors;
  uint32_t minHeap;

  // Operating mode
  OperatingMode operatingMode;

  // Button state
  bool buttonPressed;
  uint32_t buttonPressStartTime;

  // Test fans mode
  uint32_t testFansStartTime;
  uint8_t currentTestFan;  // Which fan is being tested (0-3)
  bool testFanPhase;       // true = fan ON, false = pause

  // RGB LED
  uint32_t lastRgbPulse;
  uint8_t rgbColorIndex;

  // Sensor assignment mode
  uint8_t currentSensorIndex;
  uint32_t assignmentStepStartTime;
  bool addressesModified;
  bool sensorDetectedForStep;         // Sensor found for current step
  DeviceAddress detectedAddress;      // Address of detected sensor
  float detectedTemperature;          // Temperature of detected sensor
  bool confirmButtonWasPressed;       // Button was pressed (for confirmation)
  uint32_t confirmButtonPressStart;   // When confirm button was first pressed
  uint32_t confirmButtonReleaseTime;  // When confirm button was released
  bool awaitingConfirmRelease;        // Waiting for 1s after button release
  bool awaitingSensorRemoval;         // Waiting for sensor to be removed before next

  void init() {
    wifiConnected = false;
    mqttConnected = false;
    wifiRSSI = -100;
    wifiReconnectAttempts = 0;
    lastMQTTPublish = 0;
    lastDisplayUpdate = 0;
    lastNTPSync = 0;
    lastSensorRead = 0;
    lastStatusPublish = 0;
    lastWifiCheck = 0;
    bootTime = 0;
    displayScreen = 0;

    for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
      tempSensors[i].init();
    }
    for (int i = 0; i < NUM_FANS; i++) {
      fanStates[i] = false;
    }

    ambientTemp.init();
    ambientHum.init();
    bootCount = 0;
    publishCount = 0;
    sensorErrors = 0;
    minHeap = ESP.getFreeHeap();

    operatingMode = MODE_STARTUP;
    buttonPressed = false;
    buttonPressStartTime = 0;
    testFansStartTime = 0;
    currentTestFan = 0;
    testFanPhase = false;
    lastRgbPulse = 0;
    rgbColorIndex = 0;
    currentSensorIndex = 0;
    assignmentStepStartTime = 0;
    addressesModified = false;
    sensorDetectedForStep = false;
    memset(detectedAddress, 0, sizeof(detectedAddress));
    detectedTemperature = 0.0f;
    confirmButtonWasPressed = false;
    confirmButtonPressStart = 0;
    confirmButtonReleaseTime = 0;
    awaitingConfirmRelease = false;
    awaitingSensorRemoval = false;
  }
} state;

/**
 * @brief Display cache to prevent unnecessary screen updates
 *
 * Tracks previously displayed values to enable partial screen updates.
 */
struct DisplayCache {
  // Status bar cache
  bool wifiConnected;
  int wifiRSSI;
  bool mqttConnected;
  bool sdCardShown;
  char lastIP[16];

  // Time cache
  int8_t hour;
  int8_t minute;
  int8_t day;
  int8_t month;
  int16_t year;

  // Sensor display cache
  int8_t lastScreen;  // -1 = uninitialized
  float lastTemp;
  bool lastFanState;
  bool lastSensorValid;
  bool lastSensorEnabled;

  // Ambient cache
  float lastAmbientTemp;
  float lastAmbientHum;
  bool lastAmbientTempValid;
  bool lastAmbientHumValid;

  void init() {
    wifiConnected = false;
    wifiRSSI = -999;
    mqttConnected = false;
    sdCardShown = false;
    lastIP[0] = '\0';
    hour = -1;
    minute = -1;
    day = -1;
    month = -1;
    year = -1;
    lastScreen = -1;
    lastTemp = -999.0f;
    lastFanState = false;
    lastSensorValid = false;
    lastSensorEnabled = false;
    lastAmbientTemp = -999.0f;
    lastAmbientHum = -999.0f;
    lastAmbientTempValid = false;
    lastAmbientHumValid = false;
  }
} displayCache;

// MQTT URI and topic buffers
char mqttUri[128];
char mqttTopicTemp[64];
char mqttTopicFan[64];

// ================================ WEB SERVER ================================
AsyncWebServer webServer(80);
bool webServerInitialized = false;

// ================================ ICONS ================================
// Icon bitmaps and dimension defines are in icons.h

// ================================ FUNCTION PROTOTYPES ================================
void logMessage(const char* level, const char* fmt, ...);
void initWatchdog();
void feedWatchdog();
void loadPreferences();
void savePreferences();
void initDisplay();
void initSensors();
void initRelays();
void initWiFi();
void initMQTT();
void initNTP();
void syncNTP();
void readAllSensors();
void updateFans();
void updateDisplay();
void publishTelemetry();
void publishStatus();
void handleWiFi();
void displayStatusBar();
void displayDevice(uint8_t deviceNum);
void displayLocalTime();
void displayAmbient();
uint16_t getTemperatureColor(float temp, uint8_t deviceIndex);
bool isSensorAddressValid(uint8_t index);
void updateHeapStats();

// Configuration and SD card functions
void initConfigDefaults();
void buildMqttTopics();
bool initSDCard();
void checkSDCard();
void promptLoadConfig();
bool loadConfigFromSD();
bool writeConfigToSD();
void applyConfigKey(const char* key, const char* value);
bool parseSensorAddress(const char* str, uint8_t* addr);
void displayStartupScreen();
void displayOtaStatus(const char* line1, const char* line2, uint16_t color);

// Touch and mode functions
void initTouch();
bool isInputActive();
uint8_t checkButton();
void initRgbLed();
void updateRgbLed();
void loadSensorAddresses();
void saveSensorAddresses();
bool hasStoredSensorAddresses();
void enterTestFansMode();
void handleTestFansMode();
void displayTestFansScreen();
void enterSensorAssignmentMode();
void displaySensorAssignmentScreen();
void displayAssignmentStatusArea();
void handleClearBusMode();
void handleSensorAssignmentMode();
void advanceToNextSensor();
void exitAssignmentMode(bool saveAndReboot);
void resetStepState();

// Web server functions
void initWebServer();
String generateHtmlPage();
void handleRoot(AsyncWebServerRequest* request);
void handleFanTest(AsyncWebServerRequest* request);
void handleNotFound(AsyncWebServerRequest* request);
void handleAssignSensors(AsyncWebServerRequest* request);
void handleSaveConfig(AsyncWebServerRequest* request);
void handleFirmwareUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final);
void handleFirmwareUploadComplete(AsyncWebServerRequest* request);
void handleConfigUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final);
void handleConfigUploadComplete(AsyncWebServerRequest* request);

// Callback functions
void WiFiEvent(WiFiEvent_t event);
void onMqttConnect(esp_mqtt_client_handle_t client);
void onMqttMessage(const std::string& topic, const std::string& payload);

// ================================ LOGGING ================================
/**
 * @brief Log message with timestamp and level
 * @param level Log level string (ERROR, WARN, INFO, DEBUG)
 * @param fmt Printf-style format string
 */
void logMessage(const char* level, const char* fmt, ...) {
  char timestamp[12] = "00:00:00";
  struct tm timeinfo;

  // Use 10ms timeout to avoid blocking if NTP hasn't synced
  if (getLocalTime(&timeinfo, 10)) {
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &timeinfo);
  }

  char message[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  Serial.printf("[%s] [%s] %s\n", timestamp, level, message);
}

// ================================ WATCHDOG ================================
void initWatchdog() {
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
  };

  // Deinit first since TWDT is enabled by default in IDF 5.0+
  esp_task_wdt_deinit();

  // Re-initialize with our custom configuration
  esp_err_t result = esp_task_wdt_init(&wdt_config);
  if (result != ESP_OK) {
    LOG_ERROR("Watchdog init failed: %d", result);
    return;
  }

  esp_task_wdt_add(NULL);
  LOG_INFO("Watchdog initialized (%ds timeout)", WDT_TIMEOUT_S);
}

void feedWatchdog() {
  esp_task_wdt_reset();
}

// ================================ PREFERENCES ================================
void loadPreferences() {
  preferences.begin("victron", true);
  state.bootCount = preferences.getUInt("bootCount", 0) + 1;
  preferences.end();
  LOG_INFO("Boot count: %lu", state.bootCount);
}

void savePreferences() {
  preferences.begin("victron", false);
  preferences.putUInt("bootCount", state.bootCount);
  preferences.end();
}

// ================================ RUNTIME CONFIG DEFAULTS ================================
/**
 * @brief Populate RuntimeConfig from compiled #define values
 *
 * Called at boot before SD card is read. All config.h values become defaults.
 */
void initConfigDefaults() {
  memset(&cfg, 0, sizeof(cfg));

  // WiFi
  strncpy(cfg.wifiSsid, WIFI_SSID, sizeof(cfg.wifiSsid) - 1);
  strncpy(cfg.wifiPassword, WIFI_PASSWORD, sizeof(cfg.wifiPassword) - 1);
  cfg.wifiConnectTimeoutMs = WIFI_CONNECT_TIMEOUT_MS;
  cfg.wifiReconnectIntervalMs = WIFI_RECONNECT_INTERVAL_MS;

  // MQTT
  strncpy(cfg.mqttHost, MQTT_HOST, sizeof(cfg.mqttHost) - 1);
  cfg.mqttPort = MQTT_PORT;
  strncpy(cfg.mqttUser, MQTT_USER, sizeof(cfg.mqttUser) - 1);
  strncpy(cfg.mqttPassword, MQTT_PASSW, sizeof(cfg.mqttPassword) - 1);
  strncpy(cfg.mqttClientName, MQTT_CLIENT_NAME, sizeof(cfg.mqttClientName) - 1);
  strncpy(cfg.mqttBaseTopic, MQTT_BASE_TOPIC, sizeof(cfg.mqttBaseTopic) - 1);
  cfg.mqttQos = MQTT_QOS;
  cfg.mqttRetain = MQTT_RETAIN;

  // NTP
  strncpy(cfg.ntpServer, NTP_SERVER, sizeof(cfg.ntpServer) - 1);
  cfg.gmtOffsetSec = GMT_OFFSET_SEC;
  cfg.daylightOffsetSec = DAYLIGHT_OFFSET_SEC;

  // Device names
  strncpy(cfg.deviceNames[0], DEVICE_NAME_0, sizeof(cfg.deviceNames[0]) - 1);
  strncpy(cfg.deviceNames[1], DEVICE_NAME_1, sizeof(cfg.deviceNames[1]) - 1);
  strncpy(cfg.deviceNames[2], DEVICE_NAME_2, sizeof(cfg.deviceNames[2]) - 1);
  strncpy(cfg.deviceNames[3], DEVICE_NAME_3, sizeof(cfg.deviceNames[3]) - 1);

  // Device topics
  strncpy(cfg.deviceTopics[0], DEVICE_TOPIC_0, sizeof(cfg.deviceTopics[0]) - 1);
  strncpy(cfg.deviceTopics[1], DEVICE_TOPIC_1, sizeof(cfg.deviceTopics[1]) - 1);
  strncpy(cfg.deviceTopics[2], DEVICE_TOPIC_2, sizeof(cfg.deviceTopics[2]) - 1);
  strncpy(cfg.deviceTopics[3], DEVICE_TOPIC_3, sizeof(cfg.deviceTopics[3]) - 1);

  // Temperature thresholds
  cfg.tempFanOn[0] = TEMP_FAN_ON_MPPT_W;
  cfg.tempFanOn[1] = TEMP_FAN_ON_MPPT_E;
  cfg.tempFanOn[2] = TEMP_FAN_ON_MULTIPLUS;
  cfg.tempFanOn[3] = TEMP_FAN_ON_BATTERIES;
  cfg.tempFanOff[0] = TEMP_FAN_OFF_MPPT_W;
  cfg.tempFanOff[1] = TEMP_FAN_OFF_MPPT_E;
  cfg.tempFanOff[2] = TEMP_FAN_OFF_MULTIPLUS;
  cfg.tempFanOff[3] = TEMP_FAN_OFF_BATTERIES;
  cfg.tempFanHysteresis = TEMP_FAN_HYSTERESIS;
  cfg.tempMinValid = TEMP_MIN_VALID;
  cfg.tempMaxValid = TEMP_MAX_VALID;

  // Timing
  cfg.sensorInterval = SENSOR_INTERVAL;
  cfg.displayInterval = DISP_INTERVAL;
  cfg.mqttInterval = MQTT_INTERVAL;
  cfg.statusInterval = STATUS_INTERVAL;
  cfg.ntpInterval = NTP_INTERVAL;

  // Sensor addresses from config.h defaults
  const uint8_t defaultAddr0[] = SENSOR_ADDR_0;
  const uint8_t defaultAddr1[] = SENSOR_ADDR_1;
  const uint8_t defaultAddr2[] = SENSOR_ADDR_2;
  const uint8_t defaultAddr3[] = SENSOR_ADDR_3;
  memcpy(cfg.sensorAddresses[0], defaultAddr0, 8);
  memcpy(cfg.sensorAddresses[1], defaultAddr1, 8);
  memcpy(cfg.sensorAddresses[2], defaultAddr2, 8);
  memcpy(cfg.sensorAddresses[3], defaultAddr3, 8);

  // Display
  cfg.tftRotation = TFT_ROTATION;
  strncpy(cfg.startupLine1, STARTUP_LINE_1, sizeof(cfg.startupLine1) - 1);
  strncpy(cfg.startupLine2, STARTUP_LINE_2, sizeof(cfg.startupLine2) - 1);
  strncpy(cfg.startupLine3, STARTUP_LINE_3, sizeof(cfg.startupLine3) - 1);

  // Fan
  cfg.fanActiveLow = FAN_ACTIVE_LOW;

  // Watchdog
  cfg.wdtTimeoutS = WDT_TIMEOUT_S;

  // SD status
  cfg.sdCardPresent = false;
  cfg.configLoaded = false;

  LOG_INFO("Config defaults initialized from compiled values");
}

// ================================ MQTT TOPIC BUILDER ================================
/**
 * @brief Build all MQTT topic strings from cfg.mqttBaseTopic
 */
void buildMqttTopics() {
  snprintf(cfg.mqttPubStatus, sizeof(cfg.mqttPubStatus), "%s/monitor/status", cfg.mqttBaseTopic);
  snprintf(cfg.mqttPubWifiRssi, sizeof(cfg.mqttPubWifiRssi), "%s/monitor/rssi", cfg.mqttBaseTopic);
  snprintf(cfg.mqttPubWifiIp, sizeof(cfg.mqttPubWifiIp), "%s/monitor/ip", cfg.mqttBaseTopic);
  snprintf(cfg.mqttPubUptime, sizeof(cfg.mqttPubUptime), "%s/monitor/uptime", cfg.mqttBaseTopic);
  snprintf(cfg.mqttPubHeapFree, sizeof(cfg.mqttPubHeapFree), "%s/monitor/heap_free", cfg.mqttBaseTopic);
  snprintf(cfg.mqttPubVersion, sizeof(cfg.mqttPubVersion), "%s/monitor/version", cfg.mqttBaseTopic);
  snprintf(cfg.mqttPubAmbTemp, sizeof(cfg.mqttPubAmbTemp), "%s/ambient/temperature", cfg.mqttBaseTopic);
  snprintf(cfg.mqttPubAmbHum, sizeof(cfg.mqttPubAmbHum), "%s/ambient/humidity", cfg.mqttBaseTopic);
  LOG_INFO("MQTT topics built (base: %s)", cfg.mqttBaseTopic);
}

// ================================ SD CARD ================================
/**
 * @brief Initialize SD card on shared SPI bus
 * @return true if SD card detected and mounted
 */
bool initSDCard() {
  if (!SD.begin(SD_CS_PIN, SPI)) {
    cfg.sdCardPresent = false;
    LOG_WARN("SD card not found");
    return false;
  }
  cfg.sdCardPresent = true;
  LOG_INFO("SD card mounted (type: %d, size: %llu MB)", SD.cardType(), SD.cardSize() / (1024 * 1024));
  return true;
}

/**
 * @brief Check if SD card is still present (or newly inserted)
 *
 * Called periodically from loop(). Updates cfg.sdCardPresent.
 * If card was removed, ends SD. If card appears, re-initializes.
 */
void checkSDCard() {
  if (cfg.sdCardPresent) {
    // Card was present - check if still accessible
    if (SD.cardType() == CARD_NONE) {
      cfg.sdCardPresent = false;
      SD.end();
      LOG_WARN("SD card removed");
    }
  } else {
    // Card was absent - check if one was inserted
    if (SD.begin(SD_CS_PIN, SPI)) {
      if (SD.cardType() != CARD_NONE) {
        cfg.sdCardPresent = true;
        LOG_INFO("SD card inserted (type: %d, size: %llu MB)", SD.cardType(), SD.cardSize() / (1024 * 1024));
        if (SD.exists("/config.ini")) {
          promptLoadConfig();
        }
      } else {
        SD.end();
      }
    }
  }
}

/**
 * @brief Prompt user to load config.ini from newly inserted SD card
 *
 * Blocks for up to 10 seconds. Touch = load config, no touch = skip.
 * Called from checkSDCard() when a new card with config.ini is detected.
 */
void promptLoadConfig() {
  LOG_INFO("SD: config.ini found on new card - prompting user");

  // Show prompt screen
  tft.fillScreen(COLOR_BLACK);
  int16_t x1, y1;
  uint16_t w, h;

  tft.setFont(font_small);
  tft.setTextColor(COLOR_CYAN);
  const char* line1 = "SD Card";
  tft.getTextBounds(line1, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, 50);
  tft.print(line1);

  const char* line2 = "config.ini";
  tft.setTextColor(COLOR_WHITE);
  tft.getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, 90);
  tft.print(line2);

  const char* line3 = "found";
  tft.getTextBounds(line3, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, 120);
  tft.print(line3);

  tft.setTextColor(COLOR_GREEN);
  const char* line4 = "Touch=Load";
  tft.getTextBounds(line4, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, 180);
  tft.print(line4);

  // Countdown loop - 10 seconds
  unsigned long startTime = millis();
  bool accepted = false;
  int lastSec = -1;

  while ((millis() - startTime) < 10000) {
    feedWatchdog();

    int remaining = 10 - (int)((millis() - startTime) / 1000);
    if (remaining != lastSec) {
      lastSec = remaining;
      // Clear countdown area and redraw
      tft.fillRect(0, 230, TFT_WIDTH, 40, COLOR_BLACK);
      tft.setFont(font_small);
      tft.setTextColor(COLOR_YELLOW);
      char countStr[16];
      snprintf(countStr, sizeof(countStr), "Skip in %ds", remaining);
      tft.getTextBounds(countStr, 0, 0, &x1, &y1, &w, &h);
      tft.setCursor((TFT_WIDTH - w) / 2, 260);
      tft.print(countStr);
    }

    if (isInputActive()) {
      accepted = true;
      break;
    }

    delay(50);
  }

  if (accepted) {
    // Show loading message
    tft.fillScreen(COLOR_BLACK);
    tft.setFont(font_small);
    tft.setTextColor(COLOR_GREEN);
    const char* loading = "Loading...";
    tft.getTextBounds(loading, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 160);
    tft.print(loading);

    loadConfigFromSD();
    buildMqttTopics();
    fanOnState = cfg.fanActiveLow ? LOW : HIGH;
    fanOffState = cfg.fanActiveLow ? HIGH : LOW;

    LOG_INFO("SD: config loaded from new card");
    delay(1000);
  } else {
    LOG_INFO("SD: user skipped config load");
  }

  // Restore normal display
  tft.fillScreen(COLOR_BLACK);
  displayCache.init();
}

/**
 * @brief Parse colon-separated hex address string into 8-byte array
 * @param str Address string like "28:D6:0D:95:F0:01:3C:B0"
 * @param addr Output 8-byte array
 * @return true if parsed successfully
 */
bool parseSensorAddress(const char* str, uint8_t* addr) {
  int values[8];
  int count = sscanf(str, "%x:%x:%x:%x:%x:%x:%x:%x",
                     &values[0], &values[1], &values[2], &values[3],
                     &values[4], &values[5], &values[6], &values[7]);
  if (count != 8) return false;
  for (int i = 0; i < 8; i++) {
    addr[i] = (uint8_t)values[i];
  }
  return true;
}

/**
 * @brief Apply a single key=value pair to RuntimeConfig
 * @param key INI key name (trimmed)
 * @param value INI value (trimmed)
 */
void applyConfigKey(const char* key, const char* value) {
  // WiFi
  if (strcmp(key, "wifi_ssid") == 0) {
    strncpy(cfg.wifiSsid, value, sizeof(cfg.wifiSsid) - 1);
    return;
  }
  if (strcmp(key, "wifi_password") == 0) {
    strncpy(cfg.wifiPassword, value, sizeof(cfg.wifiPassword) - 1);
    return;
  }
  if (strcmp(key, "wifi_connect_timeout_ms") == 0) {
    cfg.wifiConnectTimeoutMs = atol(value);
    return;
  }
  if (strcmp(key, "wifi_reconnect_interval_ms") == 0) {
    cfg.wifiReconnectIntervalMs = atol(value);
    return;
  }

  // MQTT
  if (strcmp(key, "mqtt_host") == 0) {
    strncpy(cfg.mqttHost, value, sizeof(cfg.mqttHost) - 1);
    return;
  }
  if (strcmp(key, "mqtt_port") == 0) {
    cfg.mqttPort = atoi(value);
    return;
  }
  if (strcmp(key, "mqtt_user") == 0) {
    strncpy(cfg.mqttUser, value, sizeof(cfg.mqttUser) - 1);
    return;
  }
  if (strcmp(key, "mqtt_password") == 0) {
    strncpy(cfg.mqttPassword, value, sizeof(cfg.mqttPassword) - 1);
    return;
  }
  if (strcmp(key, "mqtt_client_name") == 0) {
    strncpy(cfg.mqttClientName, value, sizeof(cfg.mqttClientName) - 1);
    return;
  }
  if (strcmp(key, "mqtt_base_topic") == 0) {
    strncpy(cfg.mqttBaseTopic, value, sizeof(cfg.mqttBaseTopic) - 1);
    return;
  }
  if (strcmp(key, "mqtt_qos") == 0) {
    cfg.mqttQos = atoi(value);
    return;
  }
  if (strcmp(key, "mqtt_retain") == 0) {
    cfg.mqttRetain = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    return;
  }

  // NTP
  if (strcmp(key, "ntp_server") == 0) {
    strncpy(cfg.ntpServer, value, sizeof(cfg.ntpServer) - 1);
    return;
  }
  if (strcmp(key, "gmt_offset_sec") == 0) {
    cfg.gmtOffsetSec = atol(value);
    return;
  }
  if (strcmp(key, "daylight_offset_sec") == 0) {
    cfg.daylightOffsetSec = atoi(value);
    return;
  }

  // Device names
  if (strcmp(key, "device_name_0") == 0) {
    strncpy(cfg.deviceNames[0], value, sizeof(cfg.deviceNames[0]) - 1);
    return;
  }
  if (strcmp(key, "device_name_1") == 0) {
    strncpy(cfg.deviceNames[1], value, sizeof(cfg.deviceNames[1]) - 1);
    return;
  }
  if (strcmp(key, "device_name_2") == 0) {
    strncpy(cfg.deviceNames[2], value, sizeof(cfg.deviceNames[2]) - 1);
    return;
  }
  if (strcmp(key, "device_name_3") == 0) {
    strncpy(cfg.deviceNames[3], value, sizeof(cfg.deviceNames[3]) - 1);
    return;
  }

  // Device topics
  if (strcmp(key, "device_topic_0") == 0) {
    strncpy(cfg.deviceTopics[0], value, sizeof(cfg.deviceTopics[0]) - 1);
    return;
  }
  if (strcmp(key, "device_topic_1") == 0) {
    strncpy(cfg.deviceTopics[1], value, sizeof(cfg.deviceTopics[1]) - 1);
    return;
  }
  if (strcmp(key, "device_topic_2") == 0) {
    strncpy(cfg.deviceTopics[2], value, sizeof(cfg.deviceTopics[2]) - 1);
    return;
  }
  if (strcmp(key, "device_topic_3") == 0) {
    strncpy(cfg.deviceTopics[3], value, sizeof(cfg.deviceTopics[3]) - 1);
    return;
  }

  // Temperature thresholds
  if (strcmp(key, "temp_fan_on_0") == 0) {
    cfg.tempFanOn[0] = atof(value);
    return;
  }
  if (strcmp(key, "temp_fan_on_1") == 0) {
    cfg.tempFanOn[1] = atof(value);
    return;
  }
  if (strcmp(key, "temp_fan_on_2") == 0) {
    cfg.tempFanOn[2] = atof(value);
    return;
  }
  if (strcmp(key, "temp_fan_on_3") == 0) {
    cfg.tempFanOn[3] = atof(value);
    return;
  }
  if (strcmp(key, "temp_fan_off_0") == 0) {
    cfg.tempFanOff[0] = atof(value);
    return;
  }
  if (strcmp(key, "temp_fan_off_1") == 0) {
    cfg.tempFanOff[1] = atof(value);
    return;
  }
  if (strcmp(key, "temp_fan_off_2") == 0) {
    cfg.tempFanOff[2] = atof(value);
    return;
  }
  if (strcmp(key, "temp_fan_off_3") == 0) {
    cfg.tempFanOff[3] = atof(value);
    return;
  }
  if (strcmp(key, "temp_fan_hysteresis") == 0) {
    cfg.tempFanHysteresis = atof(value);
    return;
  }
  if (strcmp(key, "temp_min_valid") == 0) {
    cfg.tempMinValid = atof(value);
    return;
  }
  if (strcmp(key, "temp_max_valid") == 0) {
    cfg.tempMaxValid = atof(value);
    return;
  }

  // Timing
  if (strcmp(key, "sensor_interval") == 0) {
    cfg.sensorInterval = atol(value);
    return;
  }
  if (strcmp(key, "display_interval") == 0) {
    cfg.displayInterval = atol(value);
    return;
  }
  if (strcmp(key, "mqtt_interval") == 0) {
    cfg.mqttInterval = atol(value);
    return;
  }
  if (strcmp(key, "status_interval") == 0) {
    cfg.statusInterval = atol(value);
    return;
  }
  if (strcmp(key, "ntp_interval") == 0) {
    cfg.ntpInterval = atol(value);
    return;
  }

  // Sensor addresses
  if (strcmp(key, "sensor_addr_0") == 0) {
    parseSensorAddress(value, cfg.sensorAddresses[0]);
    return;
  }
  if (strcmp(key, "sensor_addr_1") == 0) {
    parseSensorAddress(value, cfg.sensorAddresses[1]);
    return;
  }
  if (strcmp(key, "sensor_addr_2") == 0) {
    parseSensorAddress(value, cfg.sensorAddresses[2]);
    return;
  }
  if (strcmp(key, "sensor_addr_3") == 0) {
    parseSensorAddress(value, cfg.sensorAddresses[3]);
    return;
  }

  // Display
  if (strcmp(key, "tft_rotation") == 0) {
    cfg.tftRotation = atoi(value);
    return;
  }
  if (strcmp(key, "startup_line_1") == 0) {
    strncpy(cfg.startupLine1, value, sizeof(cfg.startupLine1) - 1);
    return;
  }
  if (strcmp(key, "startup_line_2") == 0) {
    strncpy(cfg.startupLine2, value, sizeof(cfg.startupLine2) - 1);
    return;
  }
  if (strcmp(key, "startup_line_3") == 0) {
    strncpy(cfg.startupLine3, value, sizeof(cfg.startupLine3) - 1);
    return;
  }

  // Fan
  if (strcmp(key, "fan_active_low") == 0) {
    cfg.fanActiveLow = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    return;
  }

  // Watchdog
  if (strcmp(key, "wdt_timeout_s") == 0) {
    cfg.wdtTimeoutS = atol(value);
    return;
  }

  LOG_WARN("Unknown config key: %s", key);
}

/**
 * @brief Load configuration from /config.ini on SD card
 * @return true if file was found and parsed
 */
bool loadConfigFromSD() {
  if (!cfg.sdCardPresent) return false;

  File file = SD.open("/config.ini", FILE_READ);
  if (!file) {
    LOG_INFO("SD: no config.ini found - creating template");
    writeConfigToSD();
    return false;
  }

  char line[128];
  int keyCount = 0;

  while (file.available()) {
    // Read line
    int len = 0;
    while (file.available() && len < (int)sizeof(line) - 1) {
      char c = file.read();
      if (c == '\n') break;
      if (c == '\r') continue;
      line[len++] = c;
    }
    line[len] = '\0';

    // Skip empty lines and comments
    if (len == 0 || line[0] == ';' || line[0] == '#') continue;

    // Find '=' separator
    char* eq = strchr(line, '=');
    if (!eq) continue;

    // Split into key and value
    *eq = '\0';
    char* key = line;
    char* value = eq + 1;

    // Trim leading/trailing whitespace from key
    while (*key == ' ' || *key == '\t') key++;
    char* keyEnd = eq - 1;
    while (keyEnd > key && (*keyEnd == ' ' || *keyEnd == '\t')) *keyEnd-- = '\0';

    // Trim leading/trailing whitespace from value
    while (*value == ' ' || *value == '\t') value++;
    char* valueEnd = value + strlen(value) - 1;
    while (valueEnd > value && (*valueEnd == ' ' || *valueEnd == '\t')) *valueEnd-- = '\0';

    if (strlen(key) > 0 && strlen(value) > 0) {
      applyConfigKey(key, value);
      keyCount++;
    }
  }

  file.close();
  cfg.configLoaded = true;
  LOG_INFO("SD: loaded %d config keys from /config.ini", keyCount);
  return true;
}

/**
 * @brief Write current runtime config to /config.ini on SD card
 * @return true if file was written successfully
 */
bool writeConfigToSD() {
  if (!cfg.sdCardPresent) {
    LOG_WARN("SD: cannot write config - no SD card");
    return false;
  }

  File file = SD.open("/config.ini", FILE_WRITE);
  if (!file) {
    LOG_ERROR("SD: failed to open /config.ini for writing");
    return false;
  }

  file.println("; =============================================================================");
  file.println("; config.ini - Runtime configuration for Victron Temperature Monitor");
  file.printf("; Generated by firmware v%s on boot\n", FIRMWARE_VERSION);
  file.println("; Only uncomment keys you want to override. Delete ; at start of line.");
  file.println("; =============================================================================");

  file.println();
  file.println("; ================================ WiFi ================================");
  file.printf("; wifi_ssid = %s\n", cfg.wifiSsid);
  file.printf("; wifi_password = %s\n", cfg.wifiPassword);
  file.printf("; wifi_connect_timeout_ms = %lu\n", cfg.wifiConnectTimeoutMs);
  file.printf("; wifi_reconnect_interval_ms = %lu\n", cfg.wifiReconnectIntervalMs);

  file.println();
  file.println("; ================================ MQTT ================================");
  file.printf("; mqtt_host = %s\n", cfg.mqttHost);
  file.printf("; mqtt_port = %d\n", cfg.mqttPort);
  file.printf("; mqtt_user = %s\n", cfg.mqttUser);
  file.printf("; mqtt_password = %s\n", cfg.mqttPassword);
  file.printf("; mqtt_client_name = %s\n", cfg.mqttClientName);
  file.printf("; mqtt_base_topic = %s\n", cfg.mqttBaseTopic);
  file.printf("; mqtt_qos = %d\n", cfg.mqttQos);
  file.printf("; mqtt_retain = %s\n", cfg.mqttRetain ? "true" : "false");

  file.println();
  file.println("; ================================ NTP ================================");
  file.printf("; ntp_server = %s\n", cfg.ntpServer);
  file.printf("; gmt_offset_sec = %ld\n", cfg.gmtOffsetSec);
  file.printf("; daylight_offset_sec = %d\n", cfg.daylightOffsetSec);

  file.println();
  file.println("; ================================ Device Names ================================");
  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    file.printf("; device_name_%d = %s\n", i, cfg.deviceNames[i]);
  }

  file.println();
  file.println("; ================================ MQTT Topic Suffixes ================================");
  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    file.printf("; device_topic_%d = %s\n", i, cfg.deviceTopics[i]);
  }

  file.println();
  file.println("; ================================ Temperature Thresholds ================================");
  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    file.printf("; temp_fan_on_%d = %.1f\n", i, cfg.tempFanOn[i]);
  }
  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    file.printf("; temp_fan_off_%d = %.1f\n", i, cfg.tempFanOff[i]);
  }
  file.printf("; temp_fan_hysteresis = %.1f\n", cfg.tempFanHysteresis);
  file.printf("; temp_min_valid = %.1f\n", cfg.tempMinValid);
  file.printf("; temp_max_valid = %.1f\n", cfg.tempMaxValid);

  file.println();
  file.println("; ================================ Timing Intervals (ms) ================================");
  file.printf("; sensor_interval = %lu\n", cfg.sensorInterval);
  file.printf("; display_interval = %lu\n", cfg.displayInterval);
  file.printf("; mqtt_interval = %lu\n", cfg.mqttInterval);
  file.printf("; status_interval = %lu\n", cfg.statusInterval);
  file.printf("; ntp_interval = %lu\n", cfg.ntpInterval);

  file.println();
  file.println("; ================================ Sensor Addresses ================================");
  file.println("; Format: colon-separated hex (28:XX:XX:XX:XX:XX:XX:XX)");
  file.println("; NVS-saved addresses from assignment mode take priority over these.");
  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    file.printf("; sensor_addr_%d = %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", i,
                cfg.sensorAddresses[i][0], cfg.sensorAddresses[i][1],
                cfg.sensorAddresses[i][2], cfg.sensorAddresses[i][3],
                cfg.sensorAddresses[i][4], cfg.sensorAddresses[i][5],
                cfg.sensorAddresses[i][6], cfg.sensorAddresses[i][7]);
  }

  file.println();
  file.println("; ================================ Display ================================");
  file.printf("; tft_rotation = %d\n", cfg.tftRotation);
  file.printf("; startup_line_1 = %s\n", cfg.startupLine1);
  file.printf("; startup_line_2 = %s\n", cfg.startupLine2);
  file.printf("; startup_line_3 = %s\n", cfg.startupLine3);

  file.println();
  file.println("; ================================ Fan Relay ================================");
  file.printf("; fan_active_low = %s\n", cfg.fanActiveLow ? "true" : "false");

  file.println();
  file.println("; ================================ Watchdog ================================");
  file.printf("; wdt_timeout_s = %lu\n", cfg.wdtTimeoutS);

  file.close();
  LOG_INFO("SD: config.ini written with current values");
  return true;
}

/**
 * @brief Display startup screen with SD card status
 *
 * Split from initDisplay() - shows title, version, sensor count, SD status.
 */
void displayStartupScreen() {
  int16_t x1, y1;
  uint16_t w, h;
  char info[20];

  tft.fillScreen(COLOR_BLACK);
  tft.setTextSize(1);

  // Title lines (from runtime config)
  tft.setTextColor(COLOR_WHITE);
  tft.setFont(font_small);

  tft.getTextBounds(cfg.startupLine1, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, 50);
  tft.print(cfg.startupLine1);

  tft.getTextBounds(cfg.startupLine2, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, 80);
  tft.print(cfg.startupLine2);

  tft.getTextBounds(cfg.startupLine3, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, 110);
  tft.print(cfg.startupLine3);

  // Version
  tft.setTextColor(COLOR_CYAN);
  snprintf(info, sizeof(info), "V%s", FIRMWARE_VERSION);
  tft.getTextBounds(info, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, 160);
  tft.print(info);

  // Sensors and Fans
  tft.setTextColor(COLOR_YELLOW);
  snprintf(info, sizeof(info), "%d Sensors", NUM_TEMP_SENSORS);
  tft.getTextBounds(info, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, 200);
  tft.print(info);

  snprintf(info, sizeof(info), "%d Fans", NUM_FANS);
  tft.getTextBounds(info, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, 230);
  tft.print(info);

  // SD card status
  tft.setFont(NULL);
  tft.setTextSize(2);
  if (cfg.sdCardPresent && cfg.configLoaded) {
    tft.setTextColor(COLOR_GREEN);
    const char* sdMsg = "SD: config loaded";
    tft.getTextBounds(sdMsg, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 280);
    tft.print(sdMsg);
  } else if (cfg.sdCardPresent) {
    tft.setTextColor(COLOR_YELLOW);
    const char* sdMsg = "SD: no config.ini";
    tft.getTextBounds(sdMsg, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 280);
    tft.print(sdMsg);
  } else {
    tft.setTextColor(COLOR_ORANGE);
    const char* sdMsg = "SD: not found";
    tft.getTextBounds(sdMsg, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 280);
    tft.print(sdMsg);
  }
}

/**
 * @brief Display a two-line OTA/upload status message centered on TFT
 * @param line1 First line text (e.g. "Uploading")
 * @param line2 Second line text (e.g. "Firmware...")
 * @param color Text color for both lines
 */
void displayOtaStatus(const char* line1, const char* line2, uint16_t color) {
  int16_t x1, y1;
  uint16_t w, h;

  tft.fillScreen(COLOR_BLACK);
  tft.setTextSize(1);
  tft.setFont(font_small);
  tft.setTextColor(color);

  tft.getTextBounds(line1, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, 140);
  tft.print(line1);

  tft.getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, 180);
  tft.print(line2);
}

// ================================ HEAP MONITORING ================================
void updateHeapStats() {
  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < state.minHeap) {
    state.minHeap = freeHeap;
  }

  // Warn if heap is getting low
  if (freeHeap < 20000) {
    LOG_WARN("Low heap: %lu bytes", freeHeap);
  }
}

// ================================ SENSOR ADDRESS VALIDATION ================================
bool isSensorAddressValid(uint8_t index) {
  if (index >= NUM_TEMP_SENSORS) return false;

  // Check for valid DS18B20 address (0x28 family, not placeholder, valid CRC)
  if (sensorAddresses[index][0] != 0x28) return false;
  if (sensorAddresses[index][1] == 0x00 && sensorAddresses[index][2] == 0x00) return false;
  // Skip CRC check here as it's done during loading
  return true;
}

// ================================ SENSOR ADDRESS PERSISTENCE ================================
// Default addresses from config.h (used as fallback if NVS is empty/invalid)
const DeviceAddress defaultSensorAddresses[NUM_TEMP_SENSORS] = {
  SENSOR_ADDR_0,
  SENSOR_ADDR_1,
  SENSOR_ADDR_2,
  SENSOR_ADDR_3
};

// Check if an address is valid (starts with 0x28 and not a placeholder)
bool isAddressValidDS18B20(const uint8_t* addr) {
  // Must start with 0x28 (DS18B20 family code)
  if (addr[0] != 0x28) return false;
  // Check it's not a placeholder (0x28,0x00,0x00,...)
  if (addr[1] == 0x00 && addr[2] == 0x00) return false;
  return true;
}

void loadSensorAddresses() {
  // Three-tier fallback: NVS (from prior assignment) > SD card config > compiled defaults
  preferences.begin("victron", true);

  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    char key[12];
    snprintf(key, sizeof(key), "sensor%d", i);

    uint8_t nvsAddr[8] = { 0 };
    size_t len = preferences.getBytes(key, nvsAddr, 8);

    if ((len == 8) && isAddressValidDS18B20(nvsAddr)) {
      // Priority 1: NVS (from sensor assignment mode)
      memcpy(sensorAddresses[i], nvsAddr, 8);
      LOG_INFO("Sensor %d: loaded from NVS: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
               i, nvsAddr[0], nvsAddr[1], nvsAddr[2], nvsAddr[3],
               nvsAddr[4], nvsAddr[5], nvsAddr[6], nvsAddr[7]);
    } else if (isAddressValidDS18B20(cfg.sensorAddresses[i])) {
      // Priority 2: SD card config (or compiled default if no SD)
      memcpy(sensorAddresses[i], cfg.sensorAddresses[i], 8);
      LOG_INFO("Sensor %d: using %s: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
               i, cfg.configLoaded ? "SD card" : "compiled default",
               cfg.sensorAddresses[i][0], cfg.sensorAddresses[i][1],
               cfg.sensorAddresses[i][2], cfg.sensorAddresses[i][3],
               cfg.sensorAddresses[i][4], cfg.sensorAddresses[i][5],
               cfg.sensorAddresses[i][6], cfg.sensorAddresses[i][7]);
    } else {
      // Priority 3: compiled default (placeholder)
      memcpy(sensorAddresses[i], defaultSensorAddresses[i], 8);
      LOG_INFO("Sensor %d: placeholder (not configured)", i);
    }
  }
  preferences.end();
  LOG_INFO("Sensor addresses loaded");
}

void saveSensorAddresses() {
  // Save to NVS for persistence across reboots
  preferences.begin("victron", false);
  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    char key[12];
    snprintf(key, sizeof(key), "sensor%d", i);
    preferences.putBytes(key, sensorAddresses[i], 8);
  }
  preferences.end();
  LOG_INFO("Sensor addresses saved to NVS");

  // Also print addresses in config.h format for user to copy
  Serial.println("\n========================================");
  Serial.println("  SENSOR ADDRESSES - Copy to config.h");
  Serial.println("========================================");
  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    Serial.printf("#define SENSOR_ADDR_%d  { 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X }  // %s\n",
                  i,
                  sensorAddresses[i][0], sensorAddresses[i][1],
                  sensorAddresses[i][2], sensorAddresses[i][3],
                  sensorAddresses[i][4], sensorAddresses[i][5],
                  sensorAddresses[i][6], sensorAddresses[i][7],
                  cfg.deviceNames[i]);
  }
  Serial.println("========================================\n");
}

bool hasStoredSensorAddresses() {
  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    if (isSensorAddressValid(i)) return true;
  }
  return false;
}

// ================================ TOUCHSCREEN ================================
void initTouch() {
  touch.begin();
  touch.setRotation(cfg.tftRotation);
  LOG_INFO("Touchscreen initialized (XPT2046, CS=GPIO%d, IRQ=GPIO%d)", TCH_CS, TCH_IRQ);
}

// Returns true if screen is touched
bool isInputActive() {
  return touch.touched();
}

// Returns: 0=no press, 1=short press (<1s), 2=medium press (1-5s), 3=long press (>5s)
uint8_t checkButton() {
  bool isPressed = isInputActive();
  uint32_t now = millis();

  if (isPressed && !state.buttonPressed) {
    // Button just pressed
    state.buttonPressed = true;
    state.buttonPressStartTime = now;
    LOG_DEBUG("Button pressed");
    return 0;
  }

  if (!isPressed && state.buttonPressed) {
    // Button released
    state.buttonPressed = false;
    uint32_t duration = now - state.buttonPressStartTime;

    // Ignore bounces/noise under 100ms
    if (duration < BUTTON_DEBOUNCE_MS) {
      return 0;
    }

    if (duration >= LONG_PRESS_MS) {
      LOG_INFO("Long press detected (%lu ms)", duration);
      return 3;  // Enter assignment mode
    } else if (duration >= SKIP_PRESS_MIN_MS) {
      LOG_INFO("Medium press detected (%lu ms)", duration);
      return 2;  // Skip sensor (1-5s)
    } else {
      LOG_INFO("Short press detected (%lu ms)", duration);
      return 1;  // Test fans (<1s)
    }
  }

  return 0;
}

// ================================ RGB LED (RAINBOW PULSE) ================================
// Rainbow colors: Red, Orange, Yellow, Green, Cyan, Blue, Violet
const uint8_t RAINBOW_COLORS[][3] = {
  { 255, 0, 0 },    // Red
  { 255, 127, 0 },  // Orange
  { 255, 255, 0 },  // Yellow
  { 0, 255, 0 },    // Green
  { 0, 255, 255 },  // Cyan
  { 0, 0, 255 },    // Blue
  { 148, 0, 211 }   // Violet
};
const uint8_t NUM_RAINBOW_COLORS = sizeof(RAINBOW_COLORS) / sizeof(RAINBOW_COLORS[0]);

void initRgbLed() {
  pinMode(RGB_LED_PIN, OUTPUT);
  neopixelWrite(RGB_LED_PIN, 0, 0, 0);  // Start with LED off
  LOG_INFO("RGB LED initialized on GPIO%d", RGB_LED_PIN);
}

void updateRgbLed() {
  uint32_t now = millis();
  uint32_t elapsed = now - state.lastRgbPulse;

  // Every 1 second, pulse the next rainbow color
  if (elapsed >= 1000) {
    // Scale color by brightness
    uint8_t r = (RAINBOW_COLORS[state.rgbColorIndex][0] * RGB_BRIGHTNESS) / 255;
    uint8_t g = (RAINBOW_COLORS[state.rgbColorIndex][1] * RGB_BRIGHTNESS) / 255;
    uint8_t b = (RAINBOW_COLORS[state.rgbColorIndex][2] * RGB_BRIGHTNESS) / 255;

    // Turn LED on with current rainbow color
    neopixelWrite(RGB_LED_PIN, r, g, b);

    // Advance to next color
    state.rgbColorIndex = (state.rgbColorIndex + 1) % NUM_RAINBOW_COLORS;
    state.lastRgbPulse = now;
  }
  // Turn LED off after pulse duration
  else if (elapsed >= RGB_PULSE_MS && elapsed < 1000) {
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);
  }
}

// ================================ TEST FANS MODE ================================
#define TEST_FAN_ON_MS 5000     // Each fan ON for 5 seconds
#define TEST_FAN_PAUSE_MS 1000  // Pause between fans

void enterTestFansMode() {
  state.operatingMode = MODE_TEST_FANS;
  state.testFansStartTime = millis();
  state.currentTestFan = 0;
  state.testFanPhase = true;  // Start with fan ON phase

  // Clear screen
  tft.fillScreen(COLOR_BLACK);

  // Ensure all fans OFF first
  for (int i = 0; i < NUM_FANS; i++) {
    digitalWrite(FAN_RELAY_PINS[i], fanOffState);
    state.fanStates[i] = false;
  }

  // Turn on first fan
  digitalWrite(FAN_RELAY_PINS[0], fanOnState);
  state.fanStates[0] = true;

  LOG_INFO("Test Fans mode: starting with Fan 1");
  displayTestFansScreen();
}

void handleTestFansMode() {
  uint32_t elapsed = millis() - state.testFansStartTime;

  if (state.testFanPhase) {
    // Fan ON phase - wait for 5 seconds
    if (elapsed >= TEST_FAN_ON_MS) {
      // Turn current fan OFF
      digitalWrite(FAN_RELAY_PINS[state.currentTestFan], fanOffState);
      state.fanStates[state.currentTestFan] = false;
      LOG_INFO("Fan %d OFF", state.currentTestFan + 1);

      // Check if this was the last fan
      if (state.currentTestFan >= NUM_FANS - 1) {
        // All fans tested - exit test mode
        LOG_INFO("Test Fans complete");

        // Clear screen and invalidate display cache for clean redraw
        tft.fillScreen(COLOR_BLACK);
        displayCache.init();

        state.operatingMode = MODE_NORMAL;
        return;
      }

      // Move to pause phase
      state.testFanPhase = false;
      state.testFansStartTime = millis();
      displayTestFansScreen();  // Update display to show pause
    }
  } else {
    // Pause phase - wait for 1 second
    if (elapsed >= TEST_FAN_PAUSE_MS) {
      // Move to next fan
      state.currentTestFan++;
      state.testFanPhase = true;
      state.testFansStartTime = millis();

      // Turn on next fan
      digitalWrite(FAN_RELAY_PINS[state.currentTestFan], fanOnState);
      state.fanStates[state.currentTestFan] = true;
      LOG_INFO("Fan %d ON", state.currentTestFan + 1);
      displayTestFansScreen();  // Update display
    }
  }
}

void displayTestFansScreen() {
  tft.setFont(NULL);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_YELLOW);

  // Clear content area below status bar
  tft.fillRect(0, STATUSBAR_HEIGHT, TFT_WIDTH, TFT_HEIGHT - STATUSBAR_HEIGHT, COLOR_BLACK);

  int16_t x1, y1;
  uint16_t w, h;

  // Title - centered
  tft.getTextBounds("FAN TEST", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, TITLE_Y);
  tft.print("FAN TEST");

  // Horizontal line under title
  tft.drawFastHLine(20, TITLE_LINE_Y, TFT_WIDTH - 40, COLOR_YELLOW);

  // Progress indicator [1/4]
  char progress[8];
  snprintf(progress, sizeof(progress), "[%d/%d]", state.currentTestFan + 1, NUM_FANS);
  tft.getTextBounds(progress, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, PROGRESS_Y);
  tft.print(progress);

  // Fan name - centered, larger
  tft.setTextSize(3);
  const char* fanName = cfg.deviceNames[state.currentTestFan];
  tft.getTextBounds(fanName, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, MODE_NAME_Y);
  tft.print(fanName);

  // Status - ON or PAUSE
  tft.setTextSize(3);
  if (state.testFanPhase) {
    tft.setTextColor(COLOR_GREEN);
    tft.getTextBounds("ON", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, STATUS_Y);
    tft.print("ON");
  } else {
    tft.setTextColor(COLOR_YELLOW);
    tft.getTextBounds("PAUSE", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, STATUS_Y);
    tft.print("PAUSE");
  }

  // Visual fan indicators at bottom
  tft.setTextSize(2);
  tft.setTextColor(COLOR_YELLOW);
  int startX = (TFT_WIDTH - (NUM_FANS * BOX_SPACING)) / 2;
  for (int i = 0; i < NUM_FANS; i++) {
    int x = startX + (i * BOX_SPACING);
    if (i == state.currentTestFan && state.testFanPhase) {
      // Current fan ON - filled box
      tft.fillRect(x, BOX_Y, BOX_SIZE, BOX_SIZE, COLOR_YELLOW);
      tft.setTextColor(COLOR_BLACK);
      tft.setCursor(x + 14, BOX_Y + 12);
      tft.print(i + 1);
      tft.setTextColor(COLOR_YELLOW);
    } else if (i < state.currentTestFan) {
      // Already tested - outline with check
      tft.drawRect(x, BOX_Y, BOX_SIZE, BOX_SIZE, COLOR_YELLOW);
      tft.setCursor(x + 8, BOX_Y + 12);
      tft.print("OK");
    } else {
      // Not yet tested - outline only
      tft.drawRect(x, BOX_Y, BOX_SIZE, BOX_SIZE, COLOR_YELLOW);
      tft.setCursor(x + 14, BOX_Y + 12);
      tft.print(i + 1);
    }
  }
}

// ================================ SENSOR ASSIGNMENT MODE ================================
void resetStepState() {
  state.sensorDetectedForStep = false;
  memset(state.detectedAddress, 0, sizeof(state.detectedAddress));
  state.detectedTemperature = 0.0f;
  state.confirmButtonWasPressed = false;
  state.confirmButtonPressStart = 0;
  state.confirmButtonReleaseTime = 0;
  state.awaitingConfirmRelease = false;
  state.awaitingSensorRemoval = false;
}

void enterSensorAssignmentMode() {
  state.operatingMode = MODE_SENSOR_CLEAR_BUS;
  state.currentSensorIndex = 0;
  state.assignmentStepStartTime = millis();
  state.addressesModified = false;
  resetStepState();

  LOG_INFO("Entering Sensor Assignment mode");

  // Clear screen and show initial display
  tft.fillScreen(COLOR_BLACK);
  displaySensorAssignmentScreen();
}

// Update only the status area - for partial screen updates
void displayAssignmentStatusArea() {
  tft.setFont(NULL);
  int16_t x1, y1;
  uint16_t w, h;

  // Clear status area (between name and boxes)
  tft.fillRect(0, 160, TFT_WIDTH, 90, COLOR_BLACK);

  if (state.awaitingSensorRemoval) {
    tft.setTextSize(3);
    tft.setTextColor(COLOR_RED);
    tft.getTextBounds("REMOVE", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 170);
    tft.print("REMOVE");
    tft.setTextSize(2);
    tft.setTextColor(COLOR_YELLOW);
    tft.getTextBounds("sensor to continue", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 210);
    tft.print("sensor to continue");
  } else if (state.sensorDetectedForStep) {
    // Sensor detected - show temperature and address
    tft.setTextSize(3);
    tft.setTextColor(COLOR_GREEN);

    char tempStr[16];
    snprintf(tempStr, sizeof(tempStr), "%.1fC", state.detectedTemperature);
    tft.getTextBounds(tempStr, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 165);
    tft.print(tempStr);

    tft.setTextSize(2);
    char addrStr[20];
    snprintf(addrStr, sizeof(addrStr), "%02X:%02X:%02X:%02X",
             state.detectedAddress[0], state.detectedAddress[1],
             state.detectedAddress[2], state.detectedAddress[3]);
    tft.getTextBounds(addrStr, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 195);
    tft.print(addrStr);

    tft.setTextColor(COLOR_YELLOW);
    tft.getTextBounds("Touch to save", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 225);
    tft.print("Touch to save");
  } else {
    // Waiting for sensor
    tft.setTextSize(3);
    tft.setTextColor(COLOR_YELLOW);
    tft.getTextBounds("Attach Sensor", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 170);
    tft.print("Attach Sensor");

    tft.setTextSize(2);
    tft.getTextBounds("Tap=skip 5s=clear", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 210);
    tft.print("Tap=skip 5s=clear");
  }
}

void displaySensorAssignmentScreen() {
  tft.setFont(NULL);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_CYAN);
  int16_t x1, y1;
  uint16_t w, h;

  // Clear content area
  tft.fillRect(0, STATUSBAR_HEIGHT, TFT_WIDTH, TFT_HEIGHT - STATUSBAR_HEIGHT, COLOR_BLACK);

  // Title - centered
  tft.getTextBounds("SENSOR SETUP", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_WIDTH - w) / 2, TITLE_Y);
  tft.print("SENSOR SETUP");

  // Horizontal line under title
  tft.drawFastHLine(20, TITLE_LINE_Y, TFT_WIDTH - 40, COLOR_CYAN);

  if (state.operatingMode == MODE_SENSOR_CLEAR_BUS) {
    // Show "Remove All Sensors" message
    tft.setTextSize(3);
    tft.setTextColor(COLOR_RED);
    tft.getTextBounds("REMOVE", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 90);
    tft.print("REMOVE");
    tft.getTextBounds("ALL", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 130);
    tft.print("ALL");
    tft.getTextBounds("SENSORS", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 170);
    tft.print("SENSORS");
  } else {
    // MODE_SENSOR_ASSIGNMENT - show current sensor
    tft.setTextSize(2);
    char progress[8];
    snprintf(progress, sizeof(progress), "[%d/%d]", state.currentSensorIndex + 1, NUM_TEMP_SENSORS);
    tft.getTextBounds(progress, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, PROGRESS_Y);
    tft.print(progress);

    // Sensor name - centered, larger
    tft.setTextSize(3);
    tft.setTextColor(COLOR_CYAN);
    const char* sensorName = cfg.deviceNames[state.currentSensorIndex];
    tft.getTextBounds(sensorName, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, MODE_NAME_Y);
    tft.print(sensorName);

    // Draw status area
    displayAssignmentStatusArea();
  }

  // Visual sensor indicators at bottom
  tft.setTextSize(2);
  tft.setTextColor(COLOR_CYAN);
  int startX = (TFT_WIDTH - (NUM_TEMP_SENSORS * BOX_SPACING)) / 2;
  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    int x = startX + (i * BOX_SPACING);
    if (i == state.currentSensorIndex) {
      if (state.sensorDetectedForStep) {
        tft.fillRect(x, BOX_Y, BOX_SIZE, BOX_SIZE, COLOR_GREEN);
        tft.setTextColor(COLOR_BLACK);
      } else {
        tft.fillRect(x, BOX_Y, BOX_SIZE, BOX_SIZE, COLOR_CYAN);
        tft.setTextColor(COLOR_BLACK);
      }
      tft.setCursor(x + 14, BOX_Y + 12);
      tft.print(i + 1);
      tft.setTextColor(COLOR_CYAN);
    } else if (i < state.currentSensorIndex) {
      tft.drawRect(x, BOX_Y, BOX_SIZE, BOX_SIZE, COLOR_CYAN);
      tft.setCursor(x + 8, BOX_Y + 12);
      tft.print("OK");
    } else {
      tft.drawRect(x, BOX_Y, BOX_SIZE, BOX_SIZE, COLOR_CYAN);
      tft.setCursor(x + 14, BOX_Y + 12);
      tft.print(i + 1);
    }
  }
}

void handleClearBusMode() {
  uint32_t elapsed = millis() - state.assignmentStepStartTime;

  // Check if bus is now clear
  oneWire.reset_search();
  byte addr[8];
  if (!oneWire.search(addr)) {
    // Bus clear - proceed to assignment
    LOG_INFO("Bus clear, starting sensor assignment");
    state.operatingMode = MODE_SENSOR_ASSIGNMENT;
    state.assignmentStepStartTime = millis();
    resetStepState();
    displaySensorAssignmentScreen();
    return;
  }

  // Timeout - reboot to normal mode
  if (elapsed >= ASSIGNMENT_TIMEOUT_MS) {
    LOG_WARN("Clear bus timeout - rebooting to normal mode");
    ESP.restart();
  }
}

void handleSensorAssignmentMode() {
  uint32_t now = millis();
  uint32_t elapsed = now - state.assignmentStepStartTime;
  bool isPressed = isInputActive();

  // State: Waiting for sensor to be removed
  if (state.awaitingSensorRemoval) {
    oneWire.reset_search();
    byte addr[8];
    if (!oneWire.search(addr)) {
      // Sensor removed - advance to next
      LOG_INFO("Sensor removed, advancing to next");
      advanceToNextSensor();
    }
    return;
  }

  // Handle button confirmation state machine
  if (state.awaitingConfirmRelease) {
    // Waiting for 1 second after button release
    if (now - state.confirmButtonReleaseTime >= CONFIRM_RELEASE_DELAY_MS) {
      // 1 second has passed - process the confirmation
      if (state.sensorDetectedForStep) {
        // Save the detected address
        memcpy(sensorAddresses[state.currentSensorIndex], state.detectedAddress, 8);
        state.addressesModified = true;
        LOG_INFO("Sensor %d saved: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                 state.currentSensorIndex,
                 state.detectedAddress[0], state.detectedAddress[1],
                 state.detectedAddress[2], state.detectedAddress[3],
                 state.detectedAddress[4], state.detectedAddress[5],
                 state.detectedAddress[6], state.detectedAddress[7]);

        // Now wait for sensor removal before advancing
        state.awaitingConfirmRelease = false;
        state.awaitingSensorRemoval = true;
        displayAssignmentStatusArea();  // Update display to show "Remove sensor"
        LOG_INFO("Waiting for sensor removal");
      } else {
        // No sensor detected - keep original address, advance immediately
        LOG_INFO("Sensor %d skipped - keeping original address", state.currentSensorIndex);
        advanceToNextSensor();
      }
      return;
    }
    // Still waiting - don't process anything else
    return;
  }

  // Track button press for confirmation
  if (isPressed) {
    if (!state.confirmButtonWasPressed) {
      // Button just pressed
      state.confirmButtonWasPressed = true;
      state.confirmButtonPressStart = now;
      LOG_DEBUG("Confirm button pressed");
    } else if (!state.sensorDetectedForStep) {
      // Button held while waiting for sensor - check for 5 second hold to clear memory
      uint32_t holdDuration = now - state.confirmButtonPressStart;
      if (holdDuration >= LONG_PRESS_MS) {
        // 5 second hold - clear this sensor's address
        LOG_INFO("Clearing sensor %d address (5s hold)", state.currentSensorIndex);

        // Set to placeholder address
        memset(sensorAddresses[state.currentSensorIndex], 0, 8);
        sensorAddresses[state.currentSensorIndex][0] = 0x28;  // DS18B20 family code
        state.addressesModified = true;

        // Display "Memory Cleared" message (scaled)
        tft.fillRect(0, 75, TFT_WIDTH, 45, COLOR_BLACK);
        tft.setFont(NULL);
        tft.setTextSize(2);
        tft.setTextColor(COLOR_MAGENTA);
        int16_t x1, y1;
        uint16_t w, h;
        tft.getTextBounds("MEMORY", 0, 0, &x1, &y1, &w, &h);
        tft.setCursor((TFT_WIDTH - w) / 2, 80);
        tft.print("MEMORY");
        tft.getTextBounds("CLEARED", 0, 0, &x1, &y1, &w, &h);
        tft.setCursor((TFT_WIDTH - w) / 2, 100);
        tft.print("CLEARED");

        delay(1000);
        feedWatchdog();

        // Reset button state and advance
        state.confirmButtonWasPressed = false;
        advanceToNextSensor();
        return;
      }
    }
  } else {
    if (state.confirmButtonWasPressed) {
      // Button just released
      uint32_t pressDuration = now - state.confirmButtonPressStart;
      state.confirmButtonWasPressed = false;

      if (pressDuration >= CONFIRM_PRESS_MIN_MS && pressDuration < LONG_PRESS_MS) {
        // Valid press (>200ms but <5s) - start release delay
        state.awaitingConfirmRelease = true;
        state.confirmButtonReleaseTime = now;
        LOG_INFO("Button released after %lu ms, waiting 1s before processing", pressDuration);
        return;
      }
      // Too short or was a long press (already handled) - ignore
    }
  }

  // If sensor not yet detected for this step, scan for one
  if (!state.sensorDetectedForStep) {
    oneWire.reset_search();
    byte addr[8];
    if (oneWire.search(addr)) {
      // Validate CRC
      if (OneWire::crc8(addr, 7) == addr[7]) {
        // Valid sensor found - store temporarily and read temperature
        memcpy(state.detectedAddress, addr, 8);
        state.sensorDetectedForStep = true;

        // Request and read temperature
        sensors.requestTemperaturesByAddress(addr);
        delay(200);  // Brief delay for conversion
        state.detectedTemperature = sensors.getTempC(addr);

        LOG_INFO("Sensor %d detected: %02X:%02X:%02X:%02X, Temp: %.1fC",
                 state.currentSensorIndex,
                 addr[0], addr[1], addr[2], addr[3],
                 state.detectedTemperature);

        // Update only the status area (partial screen update)
        displayAssignmentStatusArea();
        feedWatchdog();
      }
    }
  }

  // Timeout - reboot (no sensor connected in time)
  if (elapsed >= ASSIGNMENT_TIMEOUT_MS) {
    LOG_WARN("Sensor %d timeout - rebooting", state.currentSensorIndex);
    if (state.addressesModified) {
      saveSensorAddresses();
    }
    ESP.restart();
  }
}

void advanceToNextSensor() {
  state.currentSensorIndex++;
  state.assignmentStepStartTime = millis();
  resetStepState();

  if (state.currentSensorIndex >= NUM_TEMP_SENSORS) {
    // All sensors assigned - save and reboot
    LOG_INFO("All sensors assigned, saving and rebooting");
    tft.fillScreen(COLOR_BLACK);
    tft.setFont(NULL);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_GREEN);
    // Center text: "Storing"=7*12=84px, "addresses"/"Rebooting"=9*12=108px
    // Vertical: 3 lines * 16px + 2 gaps * 10px = 68px, start Y=(160-68)/2=46
    tft.setCursor((128 - 84) / 2, 46);
    tft.print("Storing");
    tft.setCursor((128 - 108) / 2, 72);
    tft.print("addresses");
    tft.setCursor((128 - 108) / 2, 98);
    tft.print("Rebooting");

    delay(2000);
    feedWatchdog();
    if (state.addressesModified) {
      saveSensorAddresses();
    }
    ESP.restart();
  } else {
    // Show next sensor screen
    displaySensorAssignmentScreen();
  }
}

void exitAssignmentMode(bool saveAndReboot) {
  if (saveAndReboot && state.addressesModified) {
    tft.fillScreen(COLOR_BLACK);
    tft.setFont(NULL);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_GREEN);
    int16_t x1, y1;
    uint16_t w, h;
    // Center text vertically and horizontally (scaled)
    tft.getTextBounds("Storing", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 46);
    tft.print("Storing");
    tft.getTextBounds("addresses", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 72);
    tft.print("addresses");
    tft.getTextBounds("Rebooting", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 98);
    tft.print("Rebooting");
    delay(2000);
    feedWatchdog();
    saveSensorAddresses();
    ESP.restart();
  } else {
    ESP.restart();  // Always reboot to return to normal mode cleanly
  }
}

// ================================ DISPLAY FUNCTIONS ================================

// Helper function to draw scaled bitmaps (scales by ICON_SCALE)
void drawScaledBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint16_t color) {
#if ICON_SCALE == 1
  // No scaling needed - direct draw
  tft.drawBitmap(x, y, bitmap, w, h, color);
#else
  // Scale up the bitmap by drawing each pixel as a ICON_SCALE x ICON_SCALE block
  for (int16_t j = 0; j < h; j++) {
    for (int16_t i = 0; i < w; i++) {
      // Read pixel from bitmap (1 bit per pixel, MSB first)
      uint8_t byte = bitmap[j * ((w + 7) / 8) + i / 8];
      if (byte & (0x80 >> (i & 7))) {
        // Pixel is set - draw scaled block
        tft.fillRect(x + (i * ICON_SCALE), y + (j * ICON_SCALE), ICON_SCALE, ICON_SCALE, color);
      }
    }
  }
#endif
}

void initDisplay() {
  // Initialize ILI9342 3.2" 240x320 display (hardware SPI only)
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin(10000000);
  tft.setRotation(cfg.tftRotation);
  tft.invertDisplay(false);
  tft.fillScreen(COLOR_BLACK);
}

void displayStatusBar() {
  // Determine current WiFi color band for cache comparison
  int rssiLevel = 0;  // 0=green, 1=yellow, 2=red
  if (state.wifiRSSI < -80) rssiLevel = 2;
  else if (state.wifiRSSI < -70) rssiLevel = 1;

  int cachedRssiLevel = 0;
  if (displayCache.wifiRSSI < -80) cachedRssiLevel = 2;
  else if (displayCache.wifiRSSI < -70) cachedRssiLevel = 1;

  // Update WiFi indicator only if changed
  bool wifiChanged = (state.wifiConnected != displayCache.wifiConnected) || (state.wifiConnected && (rssiLevel != cachedRssiLevel));
  if (wifiChanged) {
    // Clear WiFi icon area (left side)
    tft.fillRect(0, STATUSBAR_Y, 40, STATUSBAR_HEIGHT, COLOR_BLACK);

    uint16_t wifiColor = COLOR_GREEN;
    if (rssiLevel == 1) wifiColor = COLOR_YELLOW;
    if (rssiLevel == 2) wifiColor = COLOR_RED;

    if (state.wifiConnected) {
      tft.drawBitmap(WIFI_ICON_X, WIFI_ICON_Y, wifi_icon, 32, 22, wifiColor);
    } else {
      tft.drawBitmap(WIFI_ICON_X, WIFI_ICON_Y, wifi_icon, 32, 22, COLOR_RED);
      // Draw X over disconnected icon
      tft.drawLine(WIFI_ICON_X, WIFI_ICON_Y, WIFI_ICON_X + 32, WIFI_ICON_Y + 22, COLOR_RED);
    }
    displayCache.wifiConnected = state.wifiConnected;
    displayCache.wifiRSSI = state.wifiRSSI;
  }

  // Update MQTT indicator only if changed (right side of screen)
  if (state.mqttConnected != displayCache.mqttConnected) {
    // Clear MQTT text area
    tft.fillRect(TFT_WIDTH - 60, STATUSBAR_Y, 60, STATUSBAR_HEIGHT, COLOR_BLACK);

    tft.setFont(NULL);
    tft.setTextSize(2);
    tft.setTextColor(state.mqttConnected ? COLOR_GREEN : COLOR_RED);
    tft.setCursor(TFT_WIDTH - 55, 8);
    tft.print(state.mqttConnected ? "MQTT" : "----");
    displayCache.mqttConnected = state.mqttConnected;
  }

  // Update SD card icon if state changed
  if (cfg.sdCardPresent != displayCache.sdCardShown) {
    tft.fillRect(SD_ICON_X, STATUSBAR_Y, SD_ICON_W, STATUSBAR_HEIGHT, COLOR_BLACK);
    if (cfg.sdCardPresent) {
      tft.drawBitmap(SD_ICON_X, SD_ICON_Y, sd_card_icon, SD_ICON_W, SD_ICON_H, COLOR_WHITE);
    }
    displayCache.sdCardShown = cfg.sdCardPresent;
  }

  // Update IP address - only show when WiFi connected and signal green
  bool showIP = state.wifiConnected && rssiLevel == 0;
  String ipStr = showIP ? WiFi.localIP().toString() : String("");
  const char* currentIP = ipStr.c_str();
  if (strcmp(currentIP, displayCache.lastIP) != 0) {
    tft.fillRect(IP_TEXT_X, STATUSBAR_Y, 96, STATUSBAR_HEIGHT, COLOR_BLACK);
    if (showIP) {
      tft.setFont(NULL);
      tft.setTextSize(1);
      tft.setTextColor(COLOR_WHITE);
      tft.setCursor(IP_TEXT_X, IP_TEXT_Y);
      tft.print(WiFi.localIP());
    }
    strncpy(displayCache.lastIP, currentIP, sizeof(displayCache.lastIP) - 1);
    displayCache.lastIP[sizeof(displayCache.lastIP) - 1] = '\0';
  }
}

void displayLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  tft.setTextSize(1);  // Reset before using custom fonts

  int8_t curHour = timeinfo.tm_hour;
  int8_t curMinute = timeinfo.tm_min;
  int8_t curDay = timeinfo.tm_mday;
  int8_t curMonth = timeinfo.tm_mon;
  int16_t curYear = timeinfo.tm_year + 1900;

  bool timeChanged = (curHour != displayCache.hour) || (curMinute != displayCache.minute);
  bool dateChanged = (curDay != displayCache.day) || (curMonth != displayCache.month) || (curYear != displayCache.year);

  int16_t x1, y1;
  uint16_t w, h;

  if (timeChanged) {
    // Clear time row
    tft.fillRect(0, TIME_ROW_Y, TFT_WIDTH, TIME_ROW_HEIGHT, COLOR_BLACK);

    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", curHour, curMinute);

    tft.setFont(font_large);
    tft.setTextColor(COLOR_GREEN);
    tft.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, TIME_BASELINE_Y);
    tft.print(timeStr);

    displayCache.hour = curHour;
    displayCache.minute = curMinute;
  }

  if (dateChanged) {
    // Clear date row
    tft.fillRect(0, DATE_ROW_Y, TFT_WIDTH, DATE_ROW_HEIGHT, COLOR_BLACK);

    char dayMonth[4], monthName[5], yearStr[6];
    strftime(dayMonth, sizeof(dayMonth), "%d", &timeinfo);
    strftime(monthName, sizeof(monthName), "%b", &timeinfo);
    strftime(yearStr, sizeof(yearStr), "%Y", &timeinfo);

    char dateStr[16];
    snprintf(dateStr, sizeof(dateStr), "%s %s %s", dayMonth, monthName, yearStr);

    tft.setFont(font_small);
    tft.setTextColor(COLOR_CYAN);
    tft.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, DATE_BASELINE_Y);
    tft.print(dateStr);

    displayCache.day = curDay;
    displayCache.month = curMonth;
    displayCache.year = curYear;
  }
}

uint16_t getTemperatureColor(float temp, uint8_t deviceIndex) {
  if (deviceIndex >= NUM_TEMP_SENSORS) return COLOR_WHITE;
  if (temp >= cfg.tempFanOn[deviceIndex]) return COLOR_RED;
  if (temp >= cfg.tempFanOff[deviceIndex]) return COLOR_YELLOW;
  return COLOR_GREEN;
}

void displayDevice(uint8_t deviceNumber) {
  if (deviceNumber >= NUM_TEMP_SENSORS) return;

  float temp = state.tempSensors[deviceNumber].filtered;
  bool sensorValid = state.tempSensors[deviceNumber].valid;
  bool sensorEnabled = state.tempSensors[deviceNumber].enabled;
  bool fanOn = state.fanStates[deviceNumber];

  bool screenChanged = (displayCache.lastScreen != (int8_t)deviceNumber);

  // Determine color based on temperature/status
  uint16_t colour;
  if (!sensorEnabled) colour = COLOR_ORANGE;
  else if (!sensorValid) colour = COLOR_MAGENTA;
  else colour = getTemperatureColor(temp, deviceNumber);

  tft.setTextSize(1);

  // Build temperature string
  char tempStr[12];
  if (!sensorEnabled) {
    snprintf(tempStr, sizeof(tempStr), "N/C");
  } else if (sensorValid) {
    snprintf(tempStr, sizeof(tempStr), "%.1f\xB0"
                                       "C",
             temp);
  } else {
    snprintf(tempStr, sizeof(tempStr), "ERR");
  }

  int16_t x1, y1;
  uint16_t w, h;

  // Calculate name + fan icon positioning (fan icon to right of name, both centered together)
  tft.setFont(font_small);
  tft.getTextBounds(cfg.deviceNames[deviceNumber], 0, 0, &x1, &y1, &w, &h);
  int nameW = w;
  int nameH = h;
  int scaledFanW = FAN_ICON_W * ICON_SCALE;
  int scaledFanH = FAN_ICON_H * ICON_SCALE;
  int nameGap = 10;  // Gap between name and fan icon
  int totalNameWidth = nameW + (fanOn ? (nameGap + scaledFanW) : 0);
  int nameX = (TFT_WIDTH - totalNameWidth) / 2;
  int fanIconX = nameX + nameW + nameGap;
  int fanIconY = NAME_ROW_Y + (NAME_ROW_HEIGHT - scaledFanH) / 2;  // Vertically center fan icon

  // Calculate temperature icon + value positioning (centered together)
  tft.setFont(font_large);
  tft.getTextBounds(tempStr, 0, 0, &x1, &y1, &w, &h);
  int tempTextW = w;
  int scaledTempIconW = TEMP_ICON_W * ICON_SCALE;
  int scaledTempIconH = TEMP_ICON_H * ICON_SCALE;
  int tempGap = 10;  // Gap between icon and value
  int totalTempWidth = scaledTempIconW + tempGap + tempTextW;
  int tempIconX = (TFT_WIDTH - totalTempWidth) / 2;
  int tempValueX = tempIconX + scaledTempIconW + tempGap;
  int tempIconY = (TEMP_ROW_Y + (TEMP_ROW_HEIGHT - scaledTempIconH) / 2) + 16;

  if (screenChanged) {
    // Clear name row, temp row, and humidity row (leftover from ambient screen)
    tft.fillRect(0, NAME_ROW_Y, TFT_WIDTH, NAME_ROW_HEIGHT, COLOR_BLACK);
    tft.fillRect(0, TEMP_ROW_Y, TFT_WIDTH, TEMP_ROW_HEIGHT, COLOR_BLACK);
    tft.fillRect(0, HUM_ROW_Y, TFT_WIDTH, HUM_ROW_HEIGHT, COLOR_BLACK);

    // Draw device name (centered with fan icon if on)
    tft.setFont(font_small);
    tft.setTextColor(COLOR_CYAN);
    tft.setCursor(nameX, NAME_BASELINE_Y);
    tft.print(cfg.deviceNames[deviceNumber]);

    // Draw fan icon if on (to right of name, vertically centered)
    if (fanOn) {
      drawScaledBitmap(fanIconX, fanIconY, fan_icon, FAN_ICON_W, FAN_ICON_H, COLOR_CYAN);
    }

    // Draw temperature icon (centered with value)
    drawScaledBitmap(tempIconX, tempIconY, temperature_icon, TEMP_ICON_W, TEMP_ICON_H, colour);

    // Draw temperature value
    tft.setFont(font_large);
    tft.setTextColor(colour);
    tft.setCursor(tempValueX, TEMP_BASELINE_Y);
    tft.print(tempStr);

    // Update cache
    displayCache.lastScreen = (int8_t)deviceNumber;
    displayCache.lastTemp = temp;
    displayCache.lastFanState = fanOn;
    displayCache.lastSensorValid = sensorValid;
    displayCache.lastSensorEnabled = sensorEnabled;
  } else {
    // Same screen - only update changed values
    bool tempChanged = (abs(temp - displayCache.lastTemp) > 0.05f) || (sensorValid != displayCache.lastSensorValid) || (sensorEnabled != displayCache.lastSensorEnabled);
    bool fanChanged = (fanOn != displayCache.lastFanState);

    if (fanChanged) {
      // Clear and redraw entire name row (fan icon position changes)
      tft.fillRect(0, NAME_ROW_Y, TFT_WIDTH, NAME_ROW_HEIGHT, COLOR_BLACK);

      // Recalculate with new fan state
      totalNameWidth = nameW + (fanOn ? (nameGap + scaledFanW) : 0);
      nameX = (TFT_WIDTH - totalNameWidth) / 2;
      fanIconX = nameX + nameW + nameGap;

      tft.setFont(font_small);
      tft.setTextColor(COLOR_CYAN);
      tft.setCursor(nameX, NAME_BASELINE_Y);
      tft.print(cfg.deviceNames[deviceNumber]);

      if (fanOn) {
        drawScaledBitmap(fanIconX, fanIconY, fan_icon, FAN_ICON_W, FAN_ICON_H, COLOR_CYAN);
      }
      displayCache.lastFanState = fanOn;
    }

    if (tempChanged) {
      // Clear and redraw temperature row
      tft.fillRect(0, TEMP_ROW_Y, TFT_WIDTH, TEMP_ROW_HEIGHT, COLOR_BLACK);

      drawScaledBitmap(tempIconX, tempIconY, temperature_icon, TEMP_ICON_W, TEMP_ICON_H, colour);

      tft.setFont(font_large);
      tft.setTextColor(colour);
      tft.setCursor(tempValueX, TEMP_BASELINE_Y);
      tft.print(tempStr);

      displayCache.lastTemp = temp;
      displayCache.lastSensorValid = sensorValid;
      displayCache.lastSensorEnabled = sensorEnabled;
    }
  }
}

void displayAmbient() {
  const int8_t AMBIENT_SCREEN_ID = 100;

  float ambTemp = state.ambientTemp.filtered;
  float ambHum = state.ambientHum.filtered;
  bool tempValid = state.ambientTemp.valid;
  bool humValid = state.ambientHum.valid;

  bool screenChanged = (displayCache.lastScreen != AMBIENT_SCREEN_ID);

  int16_t x1, y1;
  uint16_t w, h;

  if (screenChanged) {
    // Clear name row, temp row, and humidity row
    tft.fillRect(0, NAME_ROW_Y, TFT_WIDTH, NAME_ROW_HEIGHT, COLOR_BLACK);
    tft.fillRect(0, TEMP_ROW_Y, TFT_WIDTH, TEMP_ROW_HEIGHT, COLOR_BLACK);
    tft.fillRect(0, HUM_ROW_Y, TFT_WIDTH, HUM_ROW_HEIGHT, COLOR_BLACK);

    tft.setTextSize(1);

    // "Ambient" title - centered in name row
    tft.setFont(font_small);
    tft.setTextColor(COLOR_CYAN);
    tft.getTextBounds("Ambient", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, NAME_BASELINE_Y);
    tft.print("Ambient");

    // Temperature - icon + value centered in temp row
    tft.setFont(font_large);
    char tempStr[12];
    uint16_t tempColour;
    if (tempValid) {
      tempColour = COLOR_GREEN;
      snprintf(tempStr, sizeof(tempStr), "%.1f\xB0"
                                         "C",
               ambTemp);
    } else {
      tempColour = COLOR_MAGENTA;
      snprintf(tempStr, sizeof(tempStr), "--.-\xB0"
                                         "C");
    }
    tft.getTextBounds(tempStr, 0, 0, &x1, &y1, &w, &h);
    int tempTextW = w;
    int scaledTempIconW = TEMP_ICON_W * ICON_SCALE;
    int scaledTempIconH = TEMP_ICON_H * ICON_SCALE;
    int tempGap = 10;
    int totalTempWidth = scaledTempIconW + tempGap + tempTextW;
    int tempIconX = (TFT_WIDTH - totalTempWidth) / 2;
    int tempValueX = tempIconX + scaledTempIconW + tempGap;
    int tempIconY = (TEMP_ROW_Y + (TEMP_ROW_HEIGHT - scaledTempIconH) / 2) + 16;

    drawScaledBitmap(tempIconX, tempIconY, temperature_icon, TEMP_ICON_W, TEMP_ICON_H, tempColour);
    tft.setTextColor(tempColour);
    tft.setCursor(tempValueX, TEMP_BASELINE_Y);
    tft.print(tempStr);

    // Humidity - icon + value centered in humidity row
    char humStr[12];
    uint16_t humColour;
    if (humValid) {
      humColour = COLOR_BLUE;
      snprintf(humStr, sizeof(humStr), "%.0f%%", ambHum);
    } else {
      humColour = COLOR_MAGENTA;
      snprintf(humStr, sizeof(humStr), "--%%");
    }
    tft.setFont(font_large);
    tft.getTextBounds(humStr, 0, 0, &x1, &y1, &w, &h);
    int humTextW = w;
    int humIconW = 32;
    int humIconH = 42;
    int humGap = 10;
    int totalHumWidth = humIconW + humGap + humTextW;
    int humIconX = (TFT_WIDTH - totalHumWidth) / 2;
    int humValueX = humIconX + humIconW + humGap;
    int humIconY = HUM_ROW_Y + (HUM_ROW_HEIGHT - humIconH) / 2;
    int humIconCenterY = humIconY + humIconH / 2;
    int humTextY = humIconCenterY - y1 - h / 2;

    tft.drawBitmap(humIconX, humIconY, humidity_icon_32x42, 32, 42, humColour);
    tft.setTextColor(humColour);
    tft.setCursor(humValueX, humTextY);
    tft.print(humStr);

    // Update cache
    displayCache.lastScreen = AMBIENT_SCREEN_ID;
    displayCache.lastAmbientTemp = ambTemp;
    displayCache.lastAmbientHum = ambHum;
    displayCache.lastAmbientTempValid = tempValid;
    displayCache.lastAmbientHumValid = humValid;
  } else {
    // Same screen - only update changed values
    bool tempChanged = (abs(ambTemp - displayCache.lastAmbientTemp) > 0.05f) || (tempValid != displayCache.lastAmbientTempValid);
    bool humChanged = (abs(ambHum - displayCache.lastAmbientHum) > 0.5f) || (humValid != displayCache.lastAmbientHumValid);

    if (tempChanged) {
      // Clear and redraw temperature row
      tft.fillRect(0, TEMP_ROW_Y, TFT_WIDTH, TEMP_ROW_HEIGHT, COLOR_BLACK);

      tft.setFont(font_large);
      tft.setTextSize(1);
      char tempStr[12];
      uint16_t tempColour;
      if (tempValid) {
        tempColour = COLOR_GREEN;
        snprintf(tempStr, sizeof(tempStr), "%.1f\xB0"
                                           "C",
                 ambTemp);
      } else {
        tempColour = COLOR_MAGENTA;
        snprintf(tempStr, sizeof(tempStr), "--.-\xB0"
                                           "C");
      }
      tft.getTextBounds(tempStr, 0, 0, &x1, &y1, &w, &h);
      int tempTextW = w;
      int scaledTempIconW = TEMP_ICON_W * ICON_SCALE;
      int scaledTempIconH = TEMP_ICON_H * ICON_SCALE;
      int tempGap = 10;
      int totalTempWidth = scaledTempIconW + tempGap + tempTextW;
      int tempIconX = (TFT_WIDTH - totalTempWidth) / 2;
      int tempValueX = tempIconX + scaledTempIconW + tempGap;
      int tempIconY = (TEMP_ROW_Y + (TEMP_ROW_HEIGHT - scaledTempIconH) / 2) + 16;

      drawScaledBitmap(tempIconX, tempIconY, temperature_icon, TEMP_ICON_W, TEMP_ICON_H, tempColour);
      tft.setTextColor(tempColour);
      tft.setCursor(tempValueX, TEMP_BASELINE_Y);
      tft.print(tempStr);

      displayCache.lastAmbientTemp = ambTemp;
      displayCache.lastAmbientTempValid = tempValid;
    }

    if (humChanged) {
      // Clear and redraw humidity row
      tft.fillRect(0, HUM_ROW_Y, TFT_WIDTH, HUM_ROW_HEIGHT, COLOR_BLACK);

      tft.setFont(font_large);
      tft.setTextSize(1);
      char humStr[12];
      uint16_t humColour;
      if (humValid) {
        humColour = COLOR_BLUE;
        snprintf(humStr, sizeof(humStr), "%.0f%%", ambHum);
      } else {
        humColour = COLOR_MAGENTA;
        snprintf(humStr, sizeof(humStr), "--%%");
      }
      tft.getTextBounds(humStr, 0, 0, &x1, &y1, &w, &h);
      int humTextW = w;
      int humIconW = 32;
      int humIconH = 42;
      int humGap = 10;
      int totalHumWidth = humIconW + humGap + humTextW;
      int humIconX = (TFT_WIDTH - totalHumWidth) / 2;
      int humValueX = humIconX + humIconW + humGap;
      int humIconY = HUM_ROW_Y + (HUM_ROW_HEIGHT - humIconH) / 2;
      int humIconCenterY = humIconY + humIconH / 2;
      int humTextY = humIconCenterY - y1 - h / 2;

      tft.drawBitmap(humIconX, humIconY, humidity_icon_32x42, 32, 42, humColour);
      tft.setTextColor(humColour);
      tft.setCursor(humValueX, humTextY);
      tft.print(humStr);

      displayCache.lastAmbientHum = ambHum;
      displayCache.lastAmbientHumValid = humValid;
    }
  }
}

void updateDisplay() {
  displayStatusBar();
  displayLocalTime();

  // Display current screen (ambient is NUM_TEMP_SENSORS)
  if (state.displayScreen < NUM_TEMP_SENSORS) {
    displayDevice(state.displayScreen);
  } else {
    displayAmbient();
  }

  // Advance to next enabled sensor or ambient
  do {
    state.displayScreen++;
    if (state.displayScreen > NUM_TEMP_SENSORS) {
      state.displayScreen = 0;
    }
  } while (state.displayScreen < NUM_TEMP_SENSORS && !state.tempSensors[state.displayScreen].enabled);
}

// ================================ SENSOR FUNCTIONS ================================
void initSensors() {
  sensors.begin();
  sensors.setWaitForConversion(false);
  dht.begin();

  // Log how many sensors the library found on the bus
  uint8_t deviceCount = sensors.getDeviceCount();
  LOG_INFO("DallasTemperature found %d devices on OneWire bus", deviceCount);

  uint8_t configuredCount = 0;
  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    if (isSensorAddressValid(i)) {
      // Check if this address actually exists on the bus
      bool found = sensors.isConnected(sensorAddresses[i]);
      sensors.setResolution(sensorAddresses[i], 12);
      state.tempSensors[i].enabled = true;
      configuredCount++;
      LOG_INFO("Sensor %d (%s): enabled, addr=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X, connected=%s",
               i, cfg.deviceNames[i],
               sensorAddresses[i][0], sensorAddresses[i][1],
               sensorAddresses[i][2], sensorAddresses[i][3],
               sensorAddresses[i][4], sensorAddresses[i][5],
               sensorAddresses[i][6], sensorAddresses[i][7],
               found ? "YES" : "NO");
    } else {
      state.tempSensors[i].enabled = false;
      LOG_INFO("Sensor %d (%s): disabled (invalid address)", i, cfg.deviceNames[i]);
    }
  }

  LOG_INFO("Sensors: %d DS18B20 configured, 1 DHT22", configuredCount);
}

void initRelays() {
  // FAILSAFE design: configured sensors start with fan ON until first valid reading
  // Unused sensors (invalid address) start with fan OFF
  for (int i = 0; i < NUM_FANS; i++) {
    pinMode(FAN_RELAY_PINS[i], OUTPUT);
    if (isSensorAddressValid(i)) {
      // Valid sensor - start with fan ON (failsafe until we get readings)
      digitalWrite(FAN_RELAY_PINS[i], fanOnState);
      state.fanStates[i] = true;
      LOG_INFO("Fan %d (%s): ON (failsafe start)", i, cfg.deviceNames[i]);
    } else {
      // No sensor configured - fan OFF
      digitalWrite(FAN_RELAY_PINS[i], fanOffState);
      state.fanStates[i] = false;
      LOG_INFO("Fan %d (%s): OFF (no sensor)", i, cfg.deviceNames[i]);
    }
  }
  LOG_INFO("Fan relays initialized (failsafe mode, active_%s)", cfg.fanActiveLow ? "low" : "high");
}

void readAllSensors() {
  static bool conversionStarted = false;
  static uint32_t conversionStartTime = 0;

  if (!conversionStarted) {
    sensors.requestTemperatures();
    conversionStarted = true;
    conversionStartTime = millis();
    return;
  }

  // Wait for conversion (750ms for 12-bit)
  if ((uint32_t)(millis() - conversionStartTime) < 750) {
    return;
  }

  conversionStarted = false;

  // Read DS18B20 sensors
  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    if (!state.tempSensors[i].enabled) continue;

    float temp = sensors.getTempC(sensorAddresses[i]);
    if (temp != DEVICE_DISCONNECTED_C && temp > -50.0 && temp < 85.0) {
      state.tempSensors[i].addReading(temp);
    } else {
      state.tempSensors[i].addReading(-999);
      state.sensorErrors++;
    }
  }

  // Read DHT22
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (!isnan(h)) state.ambientHum.addReading(h);
  else state.ambientHum.addReading(-999);

  if (!isnan(t)) state.ambientTemp.addReading(t);
  else state.ambientTemp.addReading(-999);
}

// ================================ FAN CONTROL ================================
void updateFans() {
  for (int i = 0; i < NUM_FANS; i++) {
    // Skip unconfigured sensors (fan stays off)
    if (!state.tempSensors[i].enabled) {
      continue;
    }

    // FAILSAFE: Sensor error on configured sensor = force fan ON
    if (!state.tempSensors[i].valid) {
      if (!state.fanStates[i]) {
        digitalWrite(FAN_RELAY_PINS[i], fanOnState);
        state.fanStates[i] = true;
        LOG_WARN("%s fan ON (sensor error - failsafe)", cfg.deviceNames[i]);
      }
      continue;
    }

    float temp = state.tempSensors[i].filtered;
    float criticalTemp = cfg.tempFanOn[i];

    // Hysteresis control
    if (temp >= criticalTemp && !state.fanStates[i]) {
      digitalWrite(FAN_RELAY_PINS[i], fanOnState);
      state.fanStates[i] = true;
      LOG_INFO("%s fan ON (%.1fC >= %.1fC)", cfg.deviceNames[i], temp, criticalTemp);
    } else if (temp < (criticalTemp - cfg.tempFanHysteresis) && state.fanStates[i]) {
      digitalWrite(FAN_RELAY_PINS[i], fanOffState);
      state.fanStates[i] = false;
      LOG_INFO("%s fan OFF (%.1fC < %.1fC)", cfg.deviceNames[i], temp, criticalTemp - cfg.tempFanHysteresis);
    }
  }
}

// ================================ WIFI FUNCTIONS ================================
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      state.wifiConnected = true;
      state.wifiReconnectAttempts = 0;
      LOG_INFO("WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      state.wifiConnected = false;
      state.mqttConnected = false;
      LOG_WARN("WiFi disconnected");
      break;
  }
}

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(WiFiEvent);
  WiFi.begin(cfg.wifiSsid, cfg.wifiPassword);

  LOG_INFO("WiFi connecting to %s...", cfg.wifiSsid);

  uint32_t startTime = millis();
  while (WiFi.status() != WL_CONNECTED && (uint32_t)(millis() - startTime) < cfg.wifiConnectTimeoutMs) {
    delay(100);
    feedWatchdog();
  }
}

void handleWiFi() {
  state.wifiConnected = (WiFi.status() == WL_CONNECTED);

  if (state.wifiConnected) {
    state.wifiRSSI = WiFi.RSSI();
  } else {
    // Exponential backoff: 30s, 60s, 120s, 240s, max 5min
    uint32_t attempts = state.wifiReconnectAttempts;
    if (attempts > 4) attempts = 4;
    uint32_t backoffTime = cfg.wifiReconnectIntervalMs * (1UL << attempts);
    if (backoffTime > 300000UL) backoffTime = 300000UL;

    if ((uint32_t)(millis() - state.lastWifiCheck) >= backoffTime) {
      state.lastWifiCheck = millis();
      state.wifiReconnectAttempts++;
      LOG_INFO("WiFi reconnect attempt %lu", state.wifiReconnectAttempts);
      WiFi.disconnect();
      WiFi.begin(cfg.wifiSsid, cfg.wifiPassword);
    }
  }
}

// ================================ MQTT FUNCTIONS ================================
// Global callback for ESP32MQTTClient library
void onMqttConnect(esp_mqtt_client_handle_t client) {
  if (mqttClient.isMyTurn(client)) {
    state.mqttConnected = true;
    LOG_INFO("MQTT connected");

    // Publish online status and version
    mqttClient.publish(cfg.mqttPubStatus, "online", cfg.mqttQos, true);
    mqttClient.publish(cfg.mqttPubVersion, FIRMWARE_VERSION, cfg.mqttQos, true);
  }
}

void onMqttMessage(const std::string& topic, const std::string& payload) {
  LOG_DEBUG("MQTT RX [%s]: %s", topic.c_str(), payload.c_str());
}

// ESP32MQTTClient event handler (required by library)
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
esp_err_t handleMQTT(esp_mqtt_event_handle_t event) {
  mqttClient.onEventCallback(event);
  return ESP_OK;
}
#else
void handleMQTT(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);
  mqttClient.onEventCallback(event);
}
#endif

void initMQTT() {
  snprintf(mqttUri, sizeof(mqttUri), "mqtt://%s:%s@%s:%d",
           cfg.mqttUser, cfg.mqttPassword, cfg.mqttHost, cfg.mqttPort);

  mqttClient.setURI(mqttUri);
  mqttClient.setMqttClientName(cfg.mqttClientName);
  mqttClient.setKeepAlive(30);
  mqttClient.enableLastWillMessage(cfg.mqttPubStatus, "offline", true);
  mqttClient.setOnMessageCallback(onMqttMessage);

  mqttClient.loopStart();
  LOG_INFO("MQTT client started");
}

void publishTelemetry() {
  if (!state.mqttConnected) return;

  char payload[16];
  uint8_t publishedCount = 0;

  // Publish only enabled temperature sensors
  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    if (!state.tempSensors[i].enabled) {
      LOG_DEBUG("Sensor %d (%s): skipped (not enabled)", i, cfg.deviceNames[i]);
      continue;
    }

    snprintf(mqttTopicTemp, sizeof(mqttTopicTemp), "%s/%s/temperature",
             cfg.mqttBaseTopic, cfg.deviceTopics[i]);

    if (state.tempSensors[i].valid) {
      snprintf(payload, sizeof(payload), "%.1f", state.tempSensors[i].filtered);
      mqttClient.publish(mqttTopicTemp, payload, cfg.mqttQos, cfg.mqttRetain);
      LOG_DEBUG("Published %s: %s", mqttTopicTemp, payload);
    }

    snprintf(mqttTopicFan, sizeof(mqttTopicFan), "%s/%s/fan",
             cfg.mqttBaseTopic, cfg.deviceTopics[i]);
    mqttClient.publish(mqttTopicFan, state.fanStates[i] ? "ON" : "OFF", cfg.mqttQos, cfg.mqttRetain);
    LOG_DEBUG("Published %s: %s", mqttTopicFan, state.fanStates[i] ? "ON" : "OFF");
    publishedCount++;
  }

  // Publish ambient data
  if (state.ambientTemp.valid) {
    snprintf(payload, sizeof(payload), "%.1f", state.ambientTemp.filtered);
    mqttClient.publish(cfg.mqttPubAmbTemp, payload, cfg.mqttQos, cfg.mqttRetain);
  }

  if (state.ambientHum.valid) {
    snprintf(payload, sizeof(payload), "%.1f", state.ambientHum.filtered);
    mqttClient.publish(cfg.mqttPubAmbHum, payload, cfg.mqttQos, cfg.mqttRetain);
  }

  state.publishCount++;
  LOG_INFO("Telemetry published: %d sensors (%lu total)", publishedCount, state.publishCount);
}

void publishStatus() {
  if (!state.mqttConnected) return;

  char payload[16];

  snprintf(payload, sizeof(payload), "%d", state.wifiRSSI);
  mqttClient.publish(cfg.mqttPubWifiRssi, payload, 0, false);

  // Publish IP address
  if (state.wifiConnected) {
    mqttClient.publish(cfg.mqttPubWifiIp, WiFi.localIP().toString().c_str(), 0, true);
  }

  uint32_t uptime = (millis() - state.bootTime) / 1000;
  snprintf(payload, sizeof(payload), "%lu", uptime);
  mqttClient.publish(cfg.mqttPubUptime, payload, 0, false);

  snprintf(payload, sizeof(payload), "%lu", ESP.getFreeHeap());
  mqttClient.publish(cfg.mqttPubHeapFree, payload, 0, false);

  LOG_DEBUG("Status published (heap: %lu)", ESP.getFreeHeap());
}

// ================================ NTP FUNCTIONS ================================
void initNTP() {
  // Use configured server as primary, public pool as fallback
  if (strlen(cfg.ntpServer) > 0) {
    configTime(cfg.gmtOffsetSec, cfg.daylightOffsetSec, cfg.ntpServer, "pool.ntp.org");
    LOG_INFO("NTP configured (primary: %s, fallback: pool.ntp.org)", cfg.ntpServer);
  } else {
    configTime(cfg.gmtOffsetSec, cfg.daylightOffsetSec, "pool.ntp.org");
    LOG_INFO("NTP configured (server: pool.ntp.org)");
  }
}

void syncNTP() {
  if (strlen(cfg.ntpServer) > 0) {
    configTime(cfg.gmtOffsetSec, cfg.daylightOffsetSec, cfg.ntpServer, "pool.ntp.org");
  } else {
    configTime(cfg.gmtOffsetSec, cfg.daylightOffsetSec, "pool.ntp.org");
  }
  LOG_INFO("NTP sync requested");
}

// ================================ WEB SERVER ================================
/**
 * @brief Generate the main HTML page with sensor data
 * @return Complete HTML string for the page
 */
String generateHtmlPage() {
  String html = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <!-- Auto-refresh via JS (pauses during firmware upload) -->
  <title>Victron Temperature Monitor</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: 'Segoe UI', Tahoma, sans-serif; background: #1a1a2e; color: #eee; padding: 20px; }
    .container { max-width: 600px; margin: 0 auto; }
    h1 { color: #00d4ff; text-align: center; margin-bottom: 20px; font-size: 1.5em; }
    .status-bar { display: flex; justify-content: space-between; background: #16213e; padding: 10px 15px; border-radius: 8px; margin-bottom: 20px; flex-wrap: wrap; gap: 10px; }
    .status-item { display: flex; align-items: center; gap: 5px; font-size: 0.9em; }
    .status-dot { width: 10px; height: 10px; border-radius: 50%; }
    .green { background: #00ff88; }
    .yellow { background: #ffcc00; }
    .red { background: #ff4444; }
    .grey { background: #666; }
    .blue { background: #00aaff; }
    .sensor-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 15px; margin-bottom: 20px; }
    .sensor-card { background: #16213e; padding: 15px; border-radius: 8px; border-left: 4px solid #666; text-align: center; }
    .sensor-card.normal { border-left-color: #00ff88; }
    .sensor-card.warning { border-left-color: #ffcc00; }
    .sensor-card.critical { border-left-color: #ff4444; }
    .sensor-card.disabled { border-left-color: #666; opacity: 0.6; }
    .sensor-card.error { border-left-color: #ff44ff; }
    .sensor-name { font-weight: bold; color: #00d4ff; margin-bottom: 5px; }
    .sensor-temp { font-size: 2em; font-weight: bold; }
    .temp-normal { color: #00ff88; }
    .temp-warning { color: #ffcc00; }
    .temp-critical { color: #ff4444; }
    .temp-error { color: #ff44ff; }
    .fan-status { display: inline-block; padding: 3px 8px; border-radius: 4px; font-size: 0.8em; margin-top: 5px; }
    .fan-on { background: #00aaff; color: #000; }
    .fan-off { background: #444; color: #999; }
    .ambient-card { background: #16213e; padding: 15px; border-radius: 8px; border-left: 4px solid #00d4ff; margin-bottom: 20px; }
    .ambient-row { display: flex; justify-content: space-around; margin-top: 10px; }
    .ambient-item { text-align: center; }
    .ambient-value { font-size: 1.8em; font-weight: bold; color: #00ff88; }
    .ambient-label { font-size: 0.8em; color: #888; }
    .button-row { display: flex; gap: 10px; margin-bottom: 20px; flex-wrap: wrap; }
    .btn { flex: 1; min-width: 120px; padding: 12px 20px; border: none; border-radius: 8px; cursor: pointer; font-size: 1em; font-weight: bold; transition: all 0.2s; }
    .btn-test { background: #00aaff; color: #000; }
    .btn-test:hover { background: #00ccff; }
    .btn-assign { background: #aa44ff; color: #fff; }
    .btn-assign:hover { background: #cc66ff; }
    .btn-save { background: #44cc44; color: #000; }
    .btn-save:hover { background: #66ee66; }
    .btn-update { background: #ff8800; color: #000; }
    .btn-update:hover { background: #ffaa00; }
    .footer { text-align: center; color: #666; font-size: 0.8em; margin-top: 20px; padding-top: 20px; border-top: 1px solid #333; }
    .upload-form { display: none; background: #16213e; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
    .upload-form.visible { display: block; }
    .upload-form input[type="file"] { margin: 10px 0; }
    .upload-form .btn-upload { background: #00ff88; color: #000; }
    .upload-progress { margin-top: 10px; }
    .config-panel { display: none; background: #16213e; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
    .config-panel.visible { display: block; }
    .config-panel h3 { margin-bottom: 10px; color: #44cc44; }
    .config-panel .divider { border-top: 1px solid #333; margin: 15px 0; }
    .config-panel label { display: flex; align-items: center; gap: 8px; margin: 10px 0; cursor: pointer; color: #ccc; }
    .config-panel input[type="checkbox"] { width: 18px; height: 18px; accent-color: #44cc44; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Victron Temperature Monitor</h1>
    <div class="status-bar">
      <div class="status-item">
        <span class="status-dot %WIFI_COLOR%"></span>
        <span>WiFi: %RSSI% dBm</span>
      </div>
      <div class="status-item">
        <span class="status-dot %MQTT_COLOR%"></span>
        <span>MQTT: %MQTT_STATUS%</span>
      </div>
      <div class="status-item">
        <span>%TIMESTAMP%</span>
      </div>
    </div>
    <div class="sensor-grid">
)rawhtml";

  // Add sensor cards
  for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
    String cardClass = "sensor-card ";
    String tempClass = "sensor-temp ";
    String tempValue = "";

    if (!state.tempSensors[i].enabled) {
      cardClass += "disabled";
      tempClass += "temp-error";
      tempValue = "N/C";
    } else if (!state.tempSensors[i].valid) {
      cardClass += "error";
      tempClass += "temp-error";
      tempValue = "ERR";
    } else {
      float temp = state.tempSensors[i].filtered;
      if (temp >= cfg.tempFanOn[i]) {
        cardClass += "critical";
        tempClass += "temp-critical";
      } else if (temp >= cfg.tempFanOff[i]) {
        cardClass += "warning";
        tempClass += "temp-warning";
      } else {
        cardClass += "normal";
        tempClass += "temp-normal";
      }
      char tempBuf[10];
      snprintf(tempBuf, sizeof(tempBuf), "%.1f&deg;C", temp);
      tempValue = String(tempBuf);
    }

    String fanClass = state.fanStates[i] ? "fan-status fan-on" : "fan-status fan-off";
    String fanText = state.fanStates[i] ? "FAN ON" : "FAN OFF";

    html += "<div class=\"" + cardClass + "\">";
    html += "<div class=\"sensor-name\">" + String(cfg.deviceNames[i]) + "</div>";
    html += "<div class=\"" + tempClass + "\">" + tempValue + "</div>";
    html += "<span class=\"" + fanClass + "\">" + fanText + "</span>";
    html += "</div>";
  }

  html += R"rawhtml(
    </div>
    <div class="ambient-card">
      <div class="sensor-name">Ambient Environment</div>
      <div class="ambient-row">
        <div class="ambient-item">
          <div class="ambient-value">%AMB_TEMP%</div>
          <div class="ambient-label">Temperature</div>
        </div>
        <div class="ambient-item">
          <div class="ambient-value" style="color: #00aaff;">%AMB_HUM%</div>
          <div class="ambient-label">Humidity</div>
        </div>
      </div>
    </div>
    <div class="button-row">
      <button class="btn btn-test" onclick="testFans()">Test Fans</button>
      <button class="btn btn-assign" onclick="assignSensors()">Assign Sensors</button>
      <button class="btn btn-save" onclick="toggleConfigPanel()">Save Config</button>
      <button class="btn btn-update" onclick="toggleUpload()">Firmware Update</button>
    </div>
    <div class="config-panel" id="configPanel">
      <h3>SD Card Configuration</h3>
      <p style="color: #888; font-size: 0.9em; margin-bottom: 15px;">Save running config to SD card or upload a config.ini file.</p>
      <div class="button-row">
        <button class="btn btn-save" onclick="saveRunningConfig()">Save Running Config</button>
      </div>
      <div class="divider"></div>
      <h3 style="color: #ff8800;">Upload config.ini</h3>
      <form id="configUploadForm" method="POST" action="/upload-config" enctype="multipart/form-data">
        <input type="file" name="config" accept=".ini,.txt" required>
        <label><input type="checkbox" name="reboot" id="configReboot"> Reboot after saving</label>
        <div class="button-row">
          <button type="submit" class="btn btn-upload">Upload & Save</button>
          <button type="button" class="btn" style="background: #666;" onclick="toggleConfigPanel()">Cancel</button>
        </div>
      </form>
      <div id="configUploadStatus"></div>
    </div>
    <div class="upload-form" id="uploadForm">
      <h3 style="margin-bottom: 10px; color: #ff8800;">Upload New Firmware</h3>
      <form method="POST" action="/update" enctype="multipart/form-data" id="firmwareForm">
        <input type="file" name="firmware" accept=".bin" required>
        <div class="button-row">
          <button type="submit" class="btn btn-upload">Upload & Install</button>
          <button type="button" class="btn" style="background: #666;" onclick="toggleUpload()">Cancel</button>
        </div>
      </form>
      <div class="upload-progress" id="uploadProgress"></div>
    </div>
    <div class="footer">
      <p>Firmware: v%VERSION% | Uptime: %UPTIME%</p>
      <p>Free Heap: %HEAP% bytes | Boot Count: %BOOTCOUNT%</p>
    </div>
  </div>
  <script>
    function testFans() {
      fetch('/test-fans').then(r => r.text()).then(t => {
        alert(t);
        setTimeout(() => location.reload(), 1000);
      });
    }
    function assignSensors() {
      if (confirm('Enter sensor assignment mode? Device will reboot after assignment.')) {
        fetch('/assign-sensors').then(r => r.text()).then(t => { alert(t); });
      }
    }
    function saveRunningConfig() {
      if (confirm('Save current running config to SD card? This will overwrite /config.ini.')) {
        fetch('/save-config').then(r => r.text()).then(t => { alert(t); });
      }
    }
    var paused = false;
    function toggleConfigPanel() {
      var el = document.getElementById('configPanel');
      el.classList.toggle('visible');
      paused = el.classList.contains('visible');
    }
    var uploading = false;
    function toggleUpload() {
      var el = document.getElementById('uploadForm');
      el.classList.toggle('visible');
      paused = el.classList.contains('visible');
    }
    document.getElementById('configUploadForm').addEventListener('submit', function(e) {
      e.preventDefault();
      uploading = true;
      var status = document.getElementById('configUploadStatus');
      status.innerHTML = '<p style="color:#ff8800;">Uploading config... please wait.</p>';
      var formData = new FormData(this);
      var reboot = document.getElementById('configReboot').checked;
      var url = '/upload-config' + (reboot ? '?reboot=1' : '');
      fetch(url, { method: 'POST', body: formData })
        .then(r => r.json())
        .then(d => {
          if (d.success) {
            status.innerHTML = '<p style="color:#00ff88;">' + d.message + '</p>';
            if (d.rebooting) {
              status.innerHTML += '<p style="color:#ff8800;">Rebooting in 3 seconds...</p>';
              setTimeout(() => location.reload(), 12000);
            } else {
              setTimeout(() => { uploading = false; location.reload(); }, 2000);
            }
          } else {
            status.innerHTML = '<p style="color:#ff4444;">' + d.message + '</p>';
            uploading = false;
          }
        })
        .catch(() => {
          status.innerHTML = '<p style="color:#ff4444;">Upload failed - connection error.</p>';
          uploading = false;
        });
    });
    document.getElementById('firmwareForm').addEventListener('submit', function(e) {
      e.preventDefault();
      uploading = true;
      var progress = document.getElementById('uploadProgress');
      progress.innerHTML = '<p style="color:#ff8800;">Uploading firmware... please wait.</p>';
      var formData = new FormData(this);
      fetch('/update', { method: 'POST', body: formData })
        .then(r => r.text())
        .then(html => {
          progress.innerHTML = '<p style="color:#00ff88;">Upload complete! Rebooting device...</p><p style="color:#888;">Page will reload in 15 seconds.</p>';
          setTimeout(() => { uploading = false; location.reload(); }, 15000);
        })
        .catch(() => {
          progress.innerHTML = '<p style="color:#00ff88;">Firmware uploaded. Device is rebooting...</p><p style="color:#888;">Page will reload in 15 seconds.</p>';
          setTimeout(() => { uploading = false; location.reload(); }, 15000);
        });
    });
    setInterval(function() { if (!uploading && !paused) location.reload(); }, 5000);
  </script>
</body>
</html>
)rawhtml";

  // Replace placeholders
  html.replace("%WIFI_COLOR%", state.wifiConnected ? (state.wifiRSSI > -70 ? "green" : (state.wifiRSSI > -80 ? "yellow" : "red")) : "red");
  html.replace("%RSSI%", String(state.wifiRSSI));
  html.replace("%MQTT_COLOR%", state.mqttConnected ? "green" : "red");
  html.replace("%MQTT_STATUS%", state.mqttConnected ? "Connected" : "Disconnected");

  // Timestamp
  struct tm timeinfo;
  char timeStr[20] = "N/A";
  if (getLocalTime(&timeinfo)) {
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  }
  html.replace("%TIMESTAMP%", String(timeStr));

  // Ambient data
  if (state.ambientTemp.valid) {
    char ambTempStr[10];
    snprintf(ambTempStr, sizeof(ambTempStr), "%.1f&deg;C", state.ambientTemp.filtered);
    html.replace("%AMB_TEMP%", String(ambTempStr));
  } else {
    html.replace("%AMB_TEMP%", "--.-&deg;C");
  }

  if (state.ambientHum.valid) {
    char ambHumStr[10];
    snprintf(ambHumStr, sizeof(ambHumStr), "%.0f%%", state.ambientHum.filtered);
    html.replace("%AMB_HUM%", String(ambHumStr));
  } else {
    html.replace("%AMB_HUM%", "--%");
  }

  // Footer info
  html.replace("%VERSION%", FIRMWARE_VERSION);

  uint32_t uptime = (millis() - state.bootTime) / 1000;
  uint32_t days = uptime / 86400;
  uint32_t hours = (uptime % 86400) / 3600;
  uint32_t minutes = (uptime % 3600) / 60;
  char uptimeStr[32];
  snprintf(uptimeStr, sizeof(uptimeStr), "%lud %luh %lum", days, hours, minutes);
  html.replace("%UPTIME%", String(uptimeStr));

  html.replace("%HEAP%", String(ESP.getFreeHeap()));
  html.replace("%BOOTCOUNT%", String(state.bootCount));

  return html;
}

/**
 * @brief Handle root page request
 */
void handleRoot(AsyncWebServerRequest* request) {
  request->send(200, "text/html", generateHtmlPage());
}

/**
 * @brief Handle fan test request
 */
void handleFanTest(AsyncWebServerRequest* request) {
  if (state.operatingMode == MODE_NORMAL) {
    enterTestFansMode();
    request->send(200, "text/plain", "Fan test started - all fans will run for 5 seconds");
  } else {
    request->send(503, "text/plain", "System busy - try again later");
  }
}

/**
 * @brief Handle save config to SD card request
 */
void handleSaveConfig(AsyncWebServerRequest* request) {
  if (writeConfigToSD()) {
    request->send(200, "text/plain", "Config saved to SD card as /config.ini");
  } else {
    request->send(500, "text/plain", "Failed to write config - check SD card");
  }
}

// Temporary file handle for config upload
static File configUploadFile;

/**
 * @brief Handle config.ini file upload data chunks
 * Writes uploaded data directly to /config.ini on SD card
 */
void handleConfigUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
  if (index == 0) {
    LOG_INFO("Config upload started: %s", filename.c_str());
    displayOtaStatus("Uploading", "Config.ini...", COLOR_YELLOW);
    if (!cfg.sdCardPresent) {
      LOG_ERROR("Config upload failed: no SD card");
      displayOtaStatus("Config Upload", "No SD Card!", COLOR_RED);
      return;
    }
    configUploadFile = SD.open("/config.ini", FILE_WRITE);
    if (!configUploadFile) {
      LOG_ERROR("Config upload failed: cannot open /config.ini for writing");
      displayOtaStatus("Config Upload", "Write Failed!", COLOR_RED);
      return;
    }
  }

  if (configUploadFile) {
    configUploadFile.write(data, len);
  }

  if (final) {
    if (configUploadFile) {
      configUploadFile.close();
      LOG_INFO("Config upload complete (%u bytes)", index + len);
      displayOtaStatus("Config.ini", "Saved to SD", COLOR_GREEN);
    }
  }
}

/**
 * @brief Handle config upload completion - apply config and optionally reboot
 */
void handleConfigUploadComplete(AsyncWebServerRequest* request) {
  bool reboot = request->hasParam("reboot") && request->getParam("reboot")->value() == "1";

  if (!cfg.sdCardPresent) {
    request->send(500, "application/json", "{\"success\":false,\"message\":\"No SD card present\",\"rebooting\":false}");
    return;
  }

  // Reload config from the newly uploaded file
  loadConfigFromSD();
  buildMqttTopics();
  fanOnState = cfg.fanActiveLow ? LOW : HIGH;
  fanOffState = cfg.fanActiveLow ? HIGH : LOW;
  LOG_INFO("Config reloaded from uploaded file");

  char json[128];
  if (reboot) {
    snprintf(json, sizeof(json), "{\"success\":true,\"message\":\"Config saved and loaded. Rebooting...\",\"rebooting\":true}");
    request->send(200, "application/json", json);
    displayOtaStatus("Rebooting...", "", COLOR_GREEN);
    delay(3000);
    ESP.restart();
  } else {
    snprintf(json, sizeof(json), "{\"success\":true,\"message\":\"Config saved and applied (no reboot).\",\"rebooting\":false}");
    request->send(200, "application/json", json);
  }
}

/**
 * @brief Handle sensor assignment mode request
 */
void handleAssignSensors(AsyncWebServerRequest* request) {
  if (state.operatingMode == MODE_NORMAL) {
    enterSensorAssignmentMode();
    request->send(200, "text/plain", "Sensor assignment mode started - use touchscreen to assign sensors");
  } else {
    request->send(503, "text/plain", "System busy - try again later");
  }
}

/**
 * @brief Handle 404 not found
 */
void handleNotFound(AsyncWebServerRequest* request) {
  request->send(404, "text/plain", "Not Found");
}

/**
 * @brief Handle firmware upload data chunks
 */
void handleFirmwareUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
  if (index == 0) {
    LOG_INFO("Firmware update started: %s", filename.c_str());
    displayOtaStatus("Uploading", "Firmware...", COLOR_YELLOW);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      LOG_ERROR("Update begin failed: %s", Update.errorString());
      displayOtaStatus("Firmware", "Upload Failed!", COLOR_RED);
      return;
    }
  }

  if (Update.write(data, len) != len) {
    LOG_ERROR("Update write failed: %s", Update.errorString());
    displayOtaStatus("Firmware", "Write Failed!", COLOR_RED);
    return;
  }

  if (final) {
    displayOtaStatus("Flashing", "Firmware...", COLOR_CYAN);
    if (Update.end(true)) {
      LOG_INFO("Firmware update complete (%u bytes)", index + len);
    } else {
      LOG_ERROR("Update end failed: %s", Update.errorString());
      displayOtaStatus("Firmware", "Flash Failed!", COLOR_RED);
    }
  }
}

/**
 * @brief Handle firmware upload completion and reboot
 */
void handleFirmwareUploadComplete(AsyncWebServerRequest* request) {
  bool success = !Update.hasError();

  String html = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Firmware Update</title>
  <style>
    body { font-family: sans-serif; background: #1a1a2e; color: #eee; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; }
    .card { background: #16213e; padding: 40px; border-radius: 12px; text-align: center; max-width: 400px; }
    .success { color: #00ff88; }
    .error { color: #ff4444; }
    h1 { margin-bottom: 20px; }
    p { color: #888; }
  </style>
</head>
<body>
  <div class="card">
)rawhtml";

  if (success) {
    html += R"rawhtml(
    <h1 class="success">Update Successful!</h1>
    <p>Device is rebooting...</p>
    <p>Page will reload in 10 seconds.</p>
    <script>setTimeout(() => location.href = '/', 10000);</script>
)rawhtml";
  } else {
    html += R"rawhtml(
    <h1 class="error">Update Failed!</h1>
    <p>)rawhtml";
    html += Update.errorString();
    html += R"rawhtml(</p>
    <p><a href="/" style="color: #00aaff;">Return to home</a></p>
)rawhtml";
  }

  html += "</div></body></html>";

  request->send(success ? 200 : 500, "text/html", html);

  if (success) {
    displayOtaStatus("Rebooting...", "", COLOR_GREEN);
    delay(1000);
    ESP.restart();
  }
}

/**
 * @brief Initialize the async web server
 */
void initWebServer() {
  if (webServerInitialized) return;

  // Main page
  webServer.on("/", HTTP_GET, handleRoot);

  // Fan test endpoint
  webServer.on("/test-fans", HTTP_GET, handleFanTest);

  // Sensor assignment endpoint
  webServer.on("/assign-sensors", HTTP_GET, handleAssignSensors);

  // Save config to SD card endpoint
  webServer.on("/save-config", HTTP_GET, handleSaveConfig);

  // Config file upload endpoint
  webServer.on("/upload-config", HTTP_POST, handleConfigUploadComplete, handleConfigUpload);

  // Firmware update endpoints
  webServer.on("/update", HTTP_POST, handleFirmwareUploadComplete, handleFirmwareUpload);

  // 404 handler
  webServer.onNotFound(handleNotFound);

  webServer.begin();
  webServerInitialized = true;

  LOG_INFO("Web server started on port 80");
  LOG_INFO("Access at http://%s/", WiFi.localIP().toString().c_str());
}

// ================================ SETUP ================================
void setup() {
  Serial.begin(115200);
  delay(100);  // Brief delay for serial to stabilize

  Serial.println("\n========================================");
  Serial.println("   Victron Temperature Monitor");
  Serial.println("   ESP32-S3-DevKitC-1");
  Serial.printf("   Version: %s\n", FIRMWARE_VERSION);
  Serial.printf("   Built: %s %s\n", BUILD_DATE, BUILD_TIME);
  Serial.printf("   Sensors: %d, Fans: %d\n", NUM_TEMP_SENSORS, NUM_FANS);
  Serial.println("========================================\n");

  // 1. Initialize runtime config from compiled defaults
  initConfigDefaults();

  state.init();
  displayCache.init();
  state.bootTime = millis();

  loadPreferences();
  savePreferences();

  // 2. Deselect all SPI CS pins HIGH before any SPI init
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  pinMode(TCH_CS, OUTPUT);
  digitalWrite(TCH_CS, HIGH);
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);

  // 3. Watchdog and display hardware init
  initWatchdog();
  initDisplay();
  initTouch();
  feedWatchdog();

  // 4. SD card: init, load config, build MQTT topics
  initSDCard();
  loadConfigFromSD();
  buildMqttTopics();
  feedWatchdog();

  // 5. Apply runtime fan polarity from config
  fanOnState = cfg.fanActiveLow ? LOW : HIGH;
  fanOffState = cfg.fanActiveLow ? HIGH : LOW;

  // 6. Re-apply rotation if SD card changed it
  tft.setRotation(cfg.tftRotation);

  // 7. Init relays (uses runtime fan polarity)
  initRelays();
  feedWatchdog();

  // 8. Load sensor addresses: NVS > SD card > compiled defaults
  loadSensorAddresses();

  // 9. Show startup screen with SD card status
  displayStartupScreen();
  delay(STARTUP_SCREEN_MS);
  feedWatchdog();

  // 10. Check if no valid addresses - auto-enter assignment mode
  if (!hasStoredSensorAddresses()) {
    LOG_INFO("No valid sensor addresses - entering assignment mode");
    enterSensorAssignmentMode();

    // Run assignment mode loop until complete
    while (state.operatingMode != MODE_NORMAL) {
      feedWatchdog();
      switch (state.operatingMode) {
        case MODE_SENSOR_CLEAR_BUS:
          handleClearBusMode();
          break;
        case MODE_SENSOR_ASSIGNMENT:
          handleSensorAssignmentMode();
          break;
        default:
          break;
      }
      delay(100);
    }
  }

  state.operatingMode = MODE_NORMAL;

  initSensors();
  feedWatchdog();

  initWiFi();
  feedWatchdog();

  initNTP();

  // Prime sensor readings
  for (int i = 0; i < 3; i++) {
    readAllSensors();
    delay(400);
    feedWatchdog();
  }

  tft.fillScreen(COLOR_BLACK);  // Clear startup screen artifacts
  displayCache.init();          // Reset cache to force full redraw
  updateDisplay();
  LOG_INFO("Setup complete");
}

// ================================ LOOP ================================
void loop() {
  uint32_t currentTime = millis();

  feedWatchdog();
  // updateRgbLed();  // Disabled - too bright

  // Handle button/touch input in normal mode
  if (state.operatingMode == MODE_NORMAL) {
    bool isPressed = isInputActive();

    // Check for 5-second hold to enter assignment mode (don't wait for release)
    if (isPressed && state.buttonPressed) {
      uint32_t holdDuration = currentTime - state.buttonPressStartTime;
      if (holdDuration >= LONG_PRESS_MS) {
        // 5 seconds held - enter assignment mode immediately
        LOG_INFO("5s hold detected - entering assignment mode");
        state.buttonPressed = false;  // Reset button state
        enterSensorAssignmentMode();
        return;
      }
    }

    // Normal button handling (for short press to test fans)
    uint8_t btn = checkButton();
    if (btn == 1) {
      // Short press (<1s) - test fans
      enterTestFansMode();
    }
    // Note: btn==3 (long press) is now handled above while still pressed
  }

  // Handle special modes
  switch (state.operatingMode) {
    case MODE_TEST_FANS:
      handleTestFansMode();
      feedWatchdog();
      return;  // Skip normal processing

    case MODE_SENSOR_CLEAR_BUS:
      handleClearBusMode();
      feedWatchdog();
      return;

    case MODE_SENSOR_ASSIGNMENT:
      handleSensorAssignmentMode();
      feedWatchdog();
      return;

    case MODE_NORMAL:
    default:
      break;  // Continue with normal loop
  }

  handleWiFi();
  feedWatchdog();

  updateHeapStats();

  // Initialize MQTT and Web Server when WiFi connects
  static bool mqttInitialized = false;
  if (state.wifiConnected && !mqttInitialized) {
    LOG_INFO("Initializing MQTT and Web Server...");
    initMQTT();
    feedWatchdog();
    initWebServer();
    feedWatchdog();
    mqttInitialized = true;
    LOG_INFO("MQTT and Web Server initialized");
  }
  if (!state.wifiConnected) {
    mqttInitialized = false;
    state.mqttConnected = false;
  }

  // Only check MQTT connection if initialized
  if (mqttInitialized) {
    state.mqttConnected = mqttClient.isConnected();
  }
  feedWatchdog();

  // Read sensors (non-blocking)
  if ((uint32_t)(currentTime - state.lastSensorRead) >= cfg.sensorInterval) {
    state.lastSensorRead = currentTime;
    readAllSensors();
    feedWatchdog();
    updateFans();
  }

  // Update display + check SD card
  if ((uint32_t)(currentTime - state.lastDisplayUpdate) >= cfg.displayInterval) {
    state.lastDisplayUpdate = currentTime;
    checkSDCard();
    updateDisplay();
    feedWatchdog();
  }

  // Publish telemetry
  if ((uint32_t)(currentTime - state.lastMQTTPublish) >= cfg.mqttInterval) {
    state.lastMQTTPublish = currentTime;
    if (state.mqttConnected) {
      publishTelemetry();
    }
    feedWatchdog();
  }

  // Publish status
  if ((uint32_t)(currentTime - state.lastStatusPublish) >= cfg.statusInterval) {
    state.lastStatusPublish = currentTime;
    if (state.mqttConnected) {
      publishStatus();
    }
    feedWatchdog();
  }

  // Sync NTP
  if ((uint32_t)(currentTime - state.lastNTPSync) >= cfg.ntpInterval) {
    state.lastNTPSync = currentTime;
    syncNTP();
    feedWatchdog();
  }

  delay(10);
}

/*
 * =============================================================================
 * DS18B20 Address Scanner
 * =============================================================================
 * Upload this sketch separately to discover your sensor addresses.
 * Copy the printed addresses to the sensorAddresses array above.
 *
 * #include <OneWire.h>
 *
 * OneWire ds(4);  // GPIO 4 (ESP32-S3)
 *
 * void setup() {
 *   Serial.begin(115200);
 *   Serial.println("\n=== DS18B20 Address Scanner ===\n");
 * }
 *
 * void loop() {
 *   byte addr[8];
 *
 *   if (!ds.search(addr)) {
 *     Serial.println("\nScan complete. Restarting in 5 seconds...\n");
 *     ds.reset_search();
 *     delay(5000);
 *     return;
 *   }
 *
 *   if (OneWire::crc8(addr, 7) != addr[7]) {
 *     Serial.println("CRC error - bad connection?");
 *     return;
 *   }
 *
 *   Serial.print("  { ");
 *   for (int i = 0; i < 8; i++) {
 *     Serial.print("0x");
 *     if (addr[i] < 16) Serial.print("0");
 *     Serial.print(addr[i], HEX);
 *     if (i < 7) Serial.print(", ");
 *   }
 *   Serial.println(" },");
 *
 *   delay(1000);
 * }
 * =============================================================================
 */
