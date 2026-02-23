# Claude Code Context: ESP32-S3 Production Arduino Development

## Project Identity
**Name:** Victron Temperature Monitor
**Platform:** ESP32-S3-DevKitC-1 (Arduino Framework)
**Firmware Version:** 2.2.0
**Purpose:** Production-grade temperature monitoring system for Victron Energy equipment
**Development Environment:** Windows 11 with Arduino CLI

## How to Use This Document

**For Claude Code users**: When starting a new development session, refer Claude to this document:
```
I'm working on the ESP32 Victron Temperature Monitor. Please read this README for complete project context before we begin.
```

**This document provides:**
- Complete hardware specifications
- Current firmware architecture
- Production code requirements
- Development workflows
- Best practices for Arduino production firmware

---

## Hardware Platform

### ESP32-S3-DevKitC-1 Specifications
```
MCU: Dual-core Xtensa LX7 @ 240 MHz
Flash: 8 MB (typical)
RAM: 512 KB SRAM + 8 MB PSRAM (if equipped)
WiFi: 802.11 b/g/n (2.4 GHz only)
Bluetooth: BLE 5.0
GPIO: 44 pins
ADC: 20 channels, 12-bit
SPI: 4 channels (SPI0-3)
I2C: 2 channels
UART: 3 channels
USB: Native USB OTG
```

### Current Peripheral Configuration

**Temperature Sensors (DS18B20):**
```
Protocol: 1-Wire (Dallas/Maxim)
GPIO: 4 (with 4.7kΩ pull-up to 3.3V)
Sensors: Up to 4 DS18B20 sensors on shared bus
  - Sensor 0: MPPT_W (MPPT West)
  - Sensor 1: MPPT_E (MPPT East)
  - Sensor 2: MultiPlus_1
  - Sensor 3: Batteries
Accuracy: ±0.5°C (-10°C to +85°C)
Resolution: 12-bit (0.0625°C)
Conversion time: 750ms
Note: Sensor addresses stored in NVS, assigned via button mode
```

**Ambient Sensor (DHT22):**
```
GPIO: 5 (with 10kΩ pull-up to 3.3V)
Measurements: Temperature + Relative Humidity
Temp Accuracy: ±0.5°C
Humidity Accuracy: ±2% RH (20-80% range)
Sampling Rate: Max 0.5 Hz (2 seconds between reads)
```

**Display (ILI9342 TFT):**
```
Resolution: 240x320 pixels
Size: 3.2 inches
Colors: 18-bit (262K colors)
Interface: SPI2
Pins:
  CS (Chip Select): GPIO 10 (SPI2 default SS)
  DC (Data/Command): GPIO 9
  RST (Reset): GPIO 8
  MOSI (Data): GPIO 11 (SPI2 default MOSI)
  SCLK (Clock): GPIO 12 (SPI2 default SCK)
  MISO: GPIO 13 (SPI2 default MISO, not used by display)
Rotation: 2 (Portrait, USB at bottom)
```

**Network:**
```
WiFi SSID: IOT (2.4 GHz network)
MQTT Broker: 192.168.40.250:1883
Username: labmqtt
NTP Server: 192.168.20.1
Timezone: GMT+2 (South Africa, no DST)
Web Interface: http://<device-ip>/ (port 80)
```

**Fan Relay Outputs (Active LOW - FAN_ACTIVE_LOW=true):**
```
Relay 0: GPIO 15 (MPPT_W cooling fan)
Relay 1: GPIO 16 (MPPT_E cooling fan)
Relay 2: GPIO 17 (MultiPlus1 cooling fan)
Relay 3: GPIO 18 (Batteries cooling fan)
Control: LOW = Fan ON, HIGH = Fan OFF
```

**SD Card (shares SPI2 bus):**
```
CS (Chip Select): GPIO 6
Interface: SPI2 (shared with TFT display and touchscreen)
Purpose: Runtime configuration via /config.ini
Library: SD.h (built into ESP32 Arduino core)
```

**Touchscreen (XPT2046):**
```
CS: GPIO 14
IRQ: GPIO 7 (low when touched)
Interface: SPI2 (shared with TFT display and SD card)
Function: Short touch (<1s) = Test fans, Long touch (>5s) = Sensor assignment mode
```

### ESP32-S3 GPIO Safety Notes
```
Strapping Pins (avoid): GPIO 0, 3, 45, 46
USB Pins (avoid): GPIO 19, 20 (USB D-/D+)
JTAG Pins (avoid): GPIO 39-42
UART0 (serial): GPIO 43, 44
PSRAM (if equipped, avoid): GPIO 26-32
Safe GPIOs used by this project: 4-18 (all safe on S3)
```

---

## Current Firmware Architecture

### Core Functionality

**1. Sensor Reading**
- DS18B20 sensors: Read 4 devices every 2 seconds via OneWire bus (GPIO 4)
- DHT22 sensor: Read ambient temperature and humidity (GPIO 5)
- 5-sample moving average filter for noise rejection
- Error detection: NaN, -127°C, out-of-range values
- 3 consecutive errors marks sensor as failed

**2. Fan Control**
- 4 relay outputs for cooling fans (GPIO 15, 16, 17, 18)
- Per-device temperature thresholds (configurable in config.h)
- 5°C hysteresis to prevent rapid cycling
- Automatic control based on temperature readings

**3. Display Management**
- ILI9342 3.2" 240x320 TFT (SPI2: GPIO 8-13)
- Rotating display of sensor temperatures (4s interval)
- Color coding: Green (normal), Yellow (warning), Red (critical)
- Larger fonts (12pt/24pt) for 240x320 display
- Non-blocking screen updates with partial redraws

**4. MQTT Publishing**
- Telemetry interval: 30 seconds
- Topics per device: `solar/<device>/temperature`, `solar/<device>/fan`
- Ambient: `solar/ambient/temperature`, `solar/ambient/humidity`
- Status: `solar/monitor/status`, `solar/monitor/version`, `solar/monitor/uptime`, `solar/monitor/rssi`, `solar/monitor/heap_free`
- QoS 1 with Last Will Testament ("offline" on disconnect)

**5. Touch Input Modes**
- Short touch (<1s): Test all fans for 5 seconds
- Long touch (>5s): Enter sensor address assignment mode
- Medium touch (1-5s): Skip sensor in assignment mode
- Assignment mode: Connect sensors one-by-one to assign addresses

**6. SD Card Configuration**
- Optional `/config.ini` on SD card overrides compiled defaults
- Auto-creates template config.ini on first boot if SD card present but no file exists
- Save current running config to SD anytime via web "Save Config" button or `/save-config` endpoint
- Supports all configurable values: WiFi, MQTT, thresholds, device names, timing
- Three-tier sensor address priority: NVS > SD card > compiled defaults
- Boot screen shows SD card status

**7. Web Interface**
- Dashboard at http://<device-ip>/ with auto-refresh (5s)
- Real-time sensor readings with color-coded status
- Fan status indicators (Blue = ON, Grey = OFF)
- WiFi RSSI and MQTT connection status
- "Test Fans" button triggers fan test sequence
- "Assign Sensors" button enters sensor assignment mode
- "Save Config" button writes current config to SD card as /config.ini
- OTA firmware update via file upload

**8. Reliability Features**
- 60s hardware watchdog timer
- Exponential backoff WiFi reconnection (30s → 5min max)
- NVS persistence for sensor addresses and boot counter
- Heap monitoring (warns below 20KB)
- Auto-recovery with graceful degradation on sensor failures

### Code Structure
```
ESP32_Victron_Dual_DS1820_MQTT_7735_v2.ino (v2.2.0)
├── Header / License / History
├── Includes (grouped by function, includes SD.h)
│   └── config.h - All compiled default settings
├── Pin Definitions (from config.h: GPIO 4-6, 8-18)
├── RuntimeConfig struct - All runtime-configurable values
├── Global Objects (OneWire, sensors, display, MQTT, preferences, webServer)
├── Data Structures
│   ├── SensorData - Moving average filter with error tracking
│   ├── RuntimeConfig - SD card overridable configuration
│   ├── SystemState - Complete system state machine
│   └── OperatingMode - Normal, TestFans, SensorClearBus, SensorAssignment
├── Configuration Functions
│   ├── initConfigDefaults() - Populate cfg from compiled #defines
│   ├── buildMqttTopics() - Build MQTT topic strings from base topic
│   ├── initSDCard() - Mount SD card on shared SPI bus
│   ├── loadConfigFromSD() - Parse /config.ini and apply overrides
│   ├── writeConfigToSD() - Write current config to SD card
│   ├── applyConfigKey() - Map INI key=value to cfg fields
│   └── displayStartupScreen() - Show boot info + SD card status
├── Core Functions
│   ├── initWatchdog() - 60s hardware watchdog with deinit/init
│   ├── initSensors() - DS18B20 + DHT22 initialization
│   ├── initDisplay() - ILI9342 TFT hardware init only
│   ├── readAllSensors() - Non-blocking sensor reads
│   ├── updateFans() - Hysteresis-based fan control (runtime polarity)
│   └── updateDisplay() - Rotating status screens
├── Touch/Mode Functions
│   ├── checkButton() - Debounced touch detection (short/medium/long)
│   ├── enterTestFansMode() - Sequential fan test (5s each)
│   ├── enterSensorAssignmentMode() - Address learning
│   └── handleClearBusMode() / handleSensorAssignmentMode()
├── Network Functions
│   ├── initWiFi() - Connection with exponential backoff
│   ├── initMQTT() - ESP32MQTTClient setup
│   ├── publishTelemetry() - Sensor data to MQTT
│   └── publishStatus() - System health to MQTT
├── Web Server Functions
│   ├── initWebServer() - AsyncWebServer on port 80
│   ├── generateHtmlPage() - Dashboard HTML generation
│   ├── handleRoot() - Main dashboard endpoint
│   ├── handleFanTest() - Trigger fan test via web
│   ├── handleAssignSensors() - Trigger assignment mode via web
│   ├── handleSaveConfig() - Save current config to SD card via web
│   ├── handleFirmwareUpload() - OTA chunked upload
│   └── handleFirmwareUploadComplete() - Reboot after OTA
├── setup() - New init order: config > SPI CS > display > SD > relays > sensors
└── loop() - Non-blocking main loop with state machine
```

### Library Dependencies
```cpp
// Core ESP32
#include <WiFi.h>
#include <time.h>
#include <SPI.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#include <Update.h>

// Sensors
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>

// Display (ILI9342 3.2" 240x320)
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>

// Network
#include <ESP32MQTTClient.h>
#include <ESPAsyncWebServer.h>  // Web dashboard + OTA
```

**Library Installation:**
```bash
arduino-cli lib install "OneWire"
arduino-cli lib install "DallasTemperature"
arduino-cli lib install "Adafruit GFX Library"
arduino-cli lib install "Adafruit BusIO"
arduino-cli lib install "Adafruit ILI9341"
arduino-cli lib install "DHT sensor library"
arduino-cli lib install "Adafruit Unified Sensor"
arduino-cli lib install "ESP32MQTTClient"
arduino-cli lib install "ESP Async WebServer"
arduino-cli lib install "Async TCP"
```

### Timing Intervals (Non-Blocking)
| Interval | Duration | Purpose |
|----------|----------|---------|
| SENSOR_INTERVAL | 2s | Read all sensors |
| DISP_INTERVAL | 4s | Cycle display screens |
| MQTT_INTERVAL | 30s | Publish telemetry |
| STATUS_INTERVAL | 1min | Publish heap/uptime |
| NTP_INTERVAL | 2h | Sync time |

### Non-Blocking Pattern Example
```cpp
void loop() {
  feedWatchdog();
  uint32_t now = millis();

  // Handle button and special modes first
  if (state.operatingMode == MODE_NORMAL) {
    uint8_t btn = checkButton();
    if (btn == 1) enterTestFansMode();      // Short press
    else if (btn == 3) enterSensorAssignmentMode();  // Long press
  }

  // Skip normal processing during special modes
  if (state.operatingMode != MODE_NORMAL) {
    handleSpecialMode();
    return;
  }

  // Timed operations with rollover-safe comparison
  if ((uint32_t)(now - state.lastSensorRead) >= SENSOR_INTERVAL) {
    state.lastSensorRead = now;
    readAllSensors();
    updateFans();
  }

  if ((uint32_t)(now - state.lastDisplayUpdate) >= DISP_INTERVAL) {
    state.lastDisplayUpdate = now;
    updateDisplay();
  }
  // ... etc
}
```

---

## Production Code Requirements

### Critical Requirements (Must Have)

**1. Reliability**
- System must run 24/7 without manual intervention
- Automatic recovery from network failures
- Graceful handling of sensor failures
- Watchdog timer to recover from hangs

**2. Error Handling**
- All sensor reads must be validated
- Network operations must handle timeouts
- Continue operation with degraded functionality when possible
- Log all errors for debugging

**3. Memory Management**
- No memory leaks (check with long-term testing)
- Use char arrays instead of String objects in loops
- Proper buffer sizing (not too large, not too small)
- Monitor heap usage during development

**4. Non-Blocking Operation**
- **Never use delay() in loop()** (except in setup())
- All timing must use millis()
- Handle millis() rollover correctly (every ~49 days)
- No long-running operations in loop()

**5. Code Quality**
- Clear, descriptive variable and function names
- Comprehensive comments explaining non-obvious logic
- Consistent formatting and style
- Modular design (functions < 50 lines typically)

### Best Practices (Should Have)

**1. Configuration Management**
```cpp
// Separate config.h file (not committed to git)
#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"
#define MQTT_BROKER "192.168.x.x"
// ... etc
```

**2. Debug Logging**
```cpp
#define DEBUG_LEVEL 2  // Compile-time debug control

#if DEBUG_LEVEL >= 1
  Serial.println("ERROR: Critical failure");
#endif

#if DEBUG_LEVEL >= 2
  Serial.printf("INFO: Published temp: %.2f\n", temp);
#endif
```

**3. Version Management**
```cpp
#define FW_VERSION "2.1.0"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

void setup() {
  Serial.printf("Firmware: v%s (%s %s)\n", 
                FW_VERSION, BUILD_DATE, BUILD_TIME);
}
```

**4. Bounds Checking**
```cpp
// Always validate sensor readings
float temp = sensors.getTempC(sensor1);
if (temp < -55 || temp > 125) {
  Serial.println("ERROR: Temperature out of range");
  return;
}
```

---

## Development Workflow

### Build Environment (Windows 11)

**Arduino CLI Setup:**
```powershell
# Install ESP32 core
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32

# Install libraries
arduino-cli lib install "OneWire"
arduino-cli lib install "DallasTemperature"
arduino-cli lib install "Adafruit GFX Library"
arduino-cli lib install "Adafruit BusIO"
arduino-cli lib install "Adafruit ILI9341"
arduino-cli lib install "DHT sensor library"
arduino-cli lib install "Adafruit Unified Sensor"
arduino-cli lib install "ESP32MQTTClient"
arduino-cli lib install "ESP Async WebServer"
arduino-cli lib install "Async TCP"
```

**Compilation:**
```powershell
$FQBN = "esp32:esp32:esp32s3"  # ESP32-S3
$PORT = "COM22"  # Adjust for your system

# Compile
arduino-cli compile --fqbn $FQBN "D:/Projects/VTM/ESP32_Victron_Dual_DS1820_MQTT_7735/ESP32_Victron_Dual_DS1820_MQTT_7735_v2"

# Upload via USB
arduino-cli upload -p $PORT --fqbn $FQBN "D:/Projects/VTM/ESP32_Victron_Dual_DS1820_MQTT_7735/ESP32_Victron_Dual_DS1820_MQTT_7735_v2"

# Serial monitor
arduino-cli monitor -p $PORT -c baudrate=115200

# Build for OTA upload (generates .bin file)
arduino-cli compile --fqbn $FQBN --output-dir ./build "D:/Projects/VTM/ESP32_Victron_Dual_DS1820_MQTT_7735/ESP32_Victron_Dual_DS1820_MQTT_7735_v2"
# Then upload the .bin via http://<device-ip>/ -> Firmware Update
```

### Testing Checklist

**Before Every Upload:**
- [ ] Code compiles without errors or warnings
- [ ] Memory usage < 80% (both flash and RAM)
- [ ] All sensor error handling in place
- [ ] No delay() calls in loop()
- [ ] Version number incremented if applicable

**After Upload:**
- [ ] Device boots successfully
- [ ] WiFi connects within 20 seconds
- [ ] MQTT connection established
- [ ] All sensors reading valid values
- [ ] Display updating correctly
- [ ] Serial output shows no errors

**Long-Term Testing:**
- [ ] 1 hour stability test (no crashes)
- [ ] 24 hour test (no memory leaks)
- [ ] WiFi disconnection/reconnection test
- [ ] MQTT broker restart test
- [ ] Sensor disconnection handling test

---

## Common Development Scenarios

### Scenario 1: Adding New Sensor
```
1. Define GPIO pin (#define)
2. Create global object for sensor
3. Add initialization in setup()
4. Create read function with error handling
5. Add to MQTT publishing section
6. Add display option if relevant
7. Update PROMPTS.md with integration example
```

### Scenario 2: Adding New Display Screen
```
1. Increment displayScreenNumMax
2. Create display function (e.g., displayStats())
3. Add case to displayDevice() switch statement
4. Follow existing pattern (clear region, set colors, format text)
5. Test screen rotation timing
```

### Scenario 3: Adding MQTT Topic
```
1. Define topic string (#define MQTT_PUB_NEWTOPIC)
2. Read sensor/calculate value
3. Add publish call in MQTT section:
   mqttClient.publish(MQTT_PUB_NEWTOPIC, String(value), 0, false);
4. Test in Home Assistant or MQTT client
5. Document in MQTT_TOPICS.md (if exists)
```

### Scenario 4: Debugging Sensor Issue
```
1. Add detailed Serial output:
   Serial.printf("Sensor read: %.2f (raw: %d)\n", temp, rawValue);
2. Check timing (sensor needs time to convert)
3. Verify wiring and pull-up resistors
4. Test sensor in isolation (simple test sketch)
5. Add retry logic if intermittent
6. Consider replace if consistently failing
```

---

## Code Patterns and Examples

### Pattern 1: Non-Blocking Timed Task
```cpp
unsigned long lastActionTime = 0;
const unsigned long ACTION_INTERVAL = 5000;  // 5 seconds

void loop() {
  unsigned long now = millis();
  
  if ((unsigned long)(now - lastActionTime) >= ACTION_INTERVAL) {
    lastActionTime = now;
    doAction();
  }
}
```

**Why this pattern:**
- Handles millis() rollover correctly (unsigned subtraction)
- Non-blocking (doesn't stop other code from running)
- Easy to adjust interval


### Pattern 2: Sensor Read with Validation
```cpp
float readTemperatureWithValidation() {
  float temp = sensors.getTempC(sensor1);
  
  // Check for sensor errors
  if (temp == -127.0) {
    Serial.println("ERROR: Sensor not detected");
    return NAN;
  }
  
  // Check for reasonable range
  if (temp < -55 || temp > 125) {
    Serial.printf("ERROR: Temp out of range: %.2f\n", temp);
    return NAN;
  }
  
  // Valid reading
  return temp;
}
```

**Usage:**
```cpp
float temp = readTemperatureWithValidation();
if (!isnan(temp)) {
  // Use temperature
  Serial.printf("Temperature: %.2f°C\n", temp);
} else {
  // Handle error (use last good value, skip publish, etc.)
}
```

### Pattern 3: State Machine for Complex Logic
```cpp
enum FanState {
  FAN_OFF,
  FAN_STARTING,
  FAN_RUNNING,
  FAN_STOPPING
};

FanState fanState = FAN_OFF;
unsigned long fanStateChangeTime = 0;

void updateFanStateMachine(float temp) {
  unsigned long now = millis();
  
  switch (fanState) {
    case FAN_OFF:
      if (temp > TEMP_FAN_ON) {
        fanState = FAN_STARTING;
        fanStateChangeTime = now;
        digitalWrite(FAN_PIN, HIGH);
      }
      break;
      
    case FAN_STARTING:
      if (now - fanStateChangeTime > FAN_START_DELAY) {
        fanState = FAN_RUNNING;
      }
      break;
      
    case FAN_RUNNING:
      if (temp < TEMP_FAN_OFF) {
        fanState = FAN_STOPPING;
        fanStateChangeTime = now;
      }
      break;
      
    case FAN_STOPPING:
      if (now - fanStateChangeTime > FAN_STOP_DELAY) {
        fanState = FAN_OFF;
        digitalWrite(FAN_PIN, LOW);
      }
      break;
  }
}
```

### Pattern 4: Exponential Backoff Retry
```cpp
const int MAX_RETRIES = 3;
const unsigned long INITIAL_DELAY = 1000;  // 1 second

bool readSensorWithRetry() {
  for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
    float temp = sensors.getTempC(sensor1);
    
    if (temp != -127.0) {
      return true;  // Success
    }
    
    // Exponential backoff: 1s, 2s, 4s
    unsigned long retryDelay = INITIAL_DELAY * (1 << attempt);
    Serial.printf("Retry %d after %lu ms\n", attempt + 1, retryDelay);
    delay(retryDelay);  // OK in dedicated function, not in loop()
  }
  
  return false;  // Failed after retries
}
```

### Pattern 5: Efficient Display Updates
```cpp
// Track previous values to avoid unnecessary redraws
static float lastDisplayedTemp = 0;
static char lastDisplayedTime[9] = "";

void updateDisplay() {
  float currentTemp = getCurrentTemp();
  char currentTime[9];
  getCurrentTime(currentTime);
  
  // Only update if changed
  if (currentTemp != lastDisplayedTemp) {
    // Clear only the temperature region
    tft.fillRect(30, 100, 98, 28, ST7735_BLACK);
    
    // Draw new temperature
    char tempStr[8];
    sprintf(tempStr, "%.1f", currentTemp);
    tft.setCursor(30, 120);
    tft.print(tempStr);
    
    lastDisplayedTemp = currentTemp;
  }
  
  if (strcmp(currentTime, lastDisplayedTime) != 0) {
    // Update time (similar pattern)
    strcpy(lastDisplayedTime, currentTime);
  }
}
```

---

## Critical "Don'ts" for Production Code

### ❌ Never Do These:

**1. Don't use delay() in loop()**
```cpp
// BAD:
void loop() {
  readSensor();
  delay(5000);  // Blocks everything!
  updateDisplay();
}

// GOOD:
void loop() {
  unsigned long now = millis();
  if (now - lastSensorRead >= 5000) {
    lastSensorRead = now;
    readSensor();
  }
  // Display can update independently
}
```

**2. Don't ignore return values**
```cpp
// BAD:
sensors.requestTemperatures();  // Ignored - might fail
float temp = sensors.getTempC(sensor1);

// GOOD:
if (sensors.requestTemperatures()) {
  float temp = sensors.getTempC(sensor1);
  if (temp != -127.0) {
    // Use temperature
  }
}
```

**3. Don't use String objects in loops**
```cpp
// BAD:
void loop() {
  String message = "Temp: " + String(temp) + " C";  // Memory fragmentation!
  mqttClient.publish(topic, message);
}

// GOOD:
void loop() {
  char message[50];
  sprintf(message, "Temp: %.2f C", temp);
  mqttClient.publish(topic, message);
}
```

**4. Don't hardcode values (use #defines)**
```cpp
// BAD:
if (temp > 50) {  // What is 50? Why 50?
  digitalWrite(25, HIGH);
}

// GOOD:
#define TEMP_CRITICAL 50  // °C threshold for fan activation
#define FAN_PIN 25

if (temp > TEMP_CRITICAL) {
  digitalWrite(FAN_PIN, HIGH);
}
```

**5. Don't assume operations succeed**
```cpp
// BAD:
WiFi.begin(ssid, password);
mqttClient.connect();  // What if WiFi not connected?

// GOOD:
WiFi.begin(ssid, password);
while (WiFi.status() != WL_CONNECTED && retries < 20) {
  delay(500);
  retries++;
}
if (WiFi.status() == WL_CONNECTED) {
  mqttClient.connect();
}
```

---

## Performance Considerations

### Memory Budget
```
ESP32-S3 RAM: 512 KB SRAM (+ 8 MB PSRAM if equipped)
ESP32-S3 System: ~80 KB
WiFi Stack: ~50 KB
Web Server: ~30 KB
Remaining: ~350+ KB

Target Usage:
- Global variables: < 50 KB
- Stack: < 30 KB
- Heap: < 150 KB
- Reserve: > 100 KB (safety margin)

Monitor with:
Serial.printf("Free heap: %lu\n", ESP.getFreeHeap());
// Heap warning threshold: 20KB (configurable)
```

### CPU Usage
```
Main loop should complete in < 10ms typically
Long operations (sensor reads, display updates) should be:
- Time-sliced (do part each iteration)
- Or infrequent (every 5-30 seconds)

Monitor with:
unsigned long loopStart = millis();
// ... loop code ...
unsigned long loopTime = millis() - loopStart;
if (loopTime > 50) {
  Serial.printf("WARNING: Long loop: %lu ms\n", loopTime);
}
```

### Power Consumption
```
Typical: 180mA @ 5V
Peak (WiFi TX): 350mA @ 5V

Power saving options:
- WiFi.setSleep(true) - Automatic sleep between transmissions
- Lower CPU frequency (not recommended - timing affected)
- Display dimming/sleep when idle
- Reduce MQTT publish frequency
```

---

## Integration with Home Assistant

### Expected MQTT Discovery (Future Feature)
```json
{
  "name": "MPPT Temperature",
  "device_class": "temperature",
  "state_topic": "victron/mppt/temperature",
  "unit_of_measurement": "°C",
  "unique_id": "victron_mppt_temp",
  "device": {
    "identifiers": ["victron_monitor_esp32"],
    "name": "Victron Temperature Monitor",
    "model": "ESP32-WROOM-32",
    "manufacturer": "Custom",
    "sw_version": "2.1.0"
  }
}
```

### Automation Examples
```yaml
# Alert on high temperature
automation:
  - alias: "MPPT Overtemp Alert"
    trigger:
      - platform: numeric_state
        entity_id: sensor.mppt_temperature
        above: 50
    action:
      - service: notify.mobile_app
        data:
          title: "MPPT High Temperature"
          message: "Temperature: {{ states('sensor.mppt_temperature') }}°C"
```

---

## When to Ask for Help

Claude Code can assist with:
✅ Implementing new features
✅ Debugging sensor issues
✅ Optimizing code performance
✅ Improving error handling
✅ Refactoring for maintainability
✅ Adding test code
✅ Creating documentation
✅ Production best practices

Provide Claude with:
1. Current code (VictronTempMonitor.ino)
2. This context document
3. Specific problem or feature request
4. Any error messages or unexpected behavior
5. Hardware changes if applicable

---

## Quick Reference Commands

```powershell
# Find ESP32-S3 port
arduino-cli board list

# Compile for ESP32-S3
arduino-cli compile --fqbn esp32:esp32:esp32s3 "D:/Projects/VTM/ESP32_Victron_Dual_DS1820_MQTT_7735/ESP32_Victron_Dual_DS1820_MQTT_7735_v2"

# Upload via USB
arduino-cli upload -p COM22 --fqbn esp32:esp32:esp32s3 "D:/Projects/VTM/ESP32_Victron_Dual_DS1820_MQTT_7735/ESP32_Victron_Dual_DS1820_MQTT_7735_v2"

# Monitor
arduino-cli monitor -p COM22 -c baudrate=115200

# Build .bin for OTA update
arduino-cli compile --fqbn esp32:esp32:esp32s3 --output-dir ./build "D:/Projects/VTM/ESP32_Victron_Dual_DS1820_MQTT_7735/ESP32_Victron_Dual_DS1820_MQTT_7735_v2"

# Check memory usage
arduino-cli compile --fqbn esp32:esp32:esp32s3 "D:/Projects/VTM/ESP32_Victron_Dual_DS1820_MQTT_7735/ESP32_Victron_Dual_DS1820_MQTT_7735_v2" --verbose | Select-String "Sketch|Global"

# Test MQTT
mosquitto_sub -h 192.168.40.250 -t "solar/#" -u labmqtt -P 7Z14kp -v

# Access Web Interface
# Open in browser: http://<device-ip>/
```

---

**Document Version:** 2.2
**Last Updated:** 2026-02-11
**Firmware Version:** 2.2.0
**For:** Production Arduino firmware development on ESP32-S3 platform
