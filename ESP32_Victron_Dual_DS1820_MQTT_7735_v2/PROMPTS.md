# Claude Code Development Prompts

**Last Updated:** 2026-02-06
**Firmware Version:** 2.0.1
**Board:** ESP32-S3-DevKitC-1 (44-pin)

---

### General Development Request
```
I'm working on production level, reliable functionality for the ESP32 Victron Temperature Monitor.

Context:
- Hardware: ESP32-S3-DevKitC-1 (44-pin)
- Temperature Sensors: 4x DS18B20 on OneWire bus (GPIO 4 with 4.7k pull-up)
- Ambient Sensor: DHT22 (GPIO 5 with 10k pull-up)
- Fan Relays: GPIO 15, 16, 17, 18 (Active LOW, FAN_ACTIVE_LOW=true)
- Button: GPIO 6 (INPUT_PULLDOWN - touch capable)
- Display: ILI9342 3.2" 240x320 TFT (SPI2: CS=10, DC=9, RST=8, MOSI=11, SCLK=12)
- Network: WiFi + MQTT publishing to Home Assistant
- Web Interface: http://device-ip/ with dashboard and OTA updates

ESP32-S3-DevKitC-1 GPIO Safety Notes to be enforced:
- Avoid strapping pins: GPIO 0, 3, 45, 46 (affect boot)
- Avoid USB pins: GPIO 19, 20 (USB D-/D+)
- Avoid JTAG pins: GPIO 39-42
- Avoid UART0 pins: GPIO 43, 44 (serial logging/programming)
- Avoid PSRAM pins (if equipped): GPIO 26-32
- Safe GPIOs: 1-18, 21, 35-38, 47-48

Requirements:
- Must follow non-blocking patterns (no delay() in loop)
- Production-ready with error handling
- Well-commented code
- Configuration values in config.h
- Icon data structure in icons.h

Iplement this following the project conventions in CLAUDE.md
```

---

## Code Quality and Refactoring

### Prompt: Review and Improve Code Quality
```
Review the current VictronTempMonitor.ino sketch and suggest improvements for production quality.

Focus areas:
0. Validate and update the Hardware pin allocations in .clauderc to ensure best practices for the development board   
1. Error handling - Are sensor failures handled gracefully? 
2. Memory management - Any String objects that should be char arrays?
3. Non-blocking code - Any delay() calls that should use millis()?
4. Code organization - Should any sections be broken into functions?
5. Documentation - Are comments clear and helpful?

For each issue found:
- Explain the problem
- Show the problematic code
- Provide corrected code
- Explain why the change improves quality

Follow conventions in .claude/CONVENTIONS.md
```

### Prompt: Refactor Specific Function
```

Requirements:
- Maintain current functionality
- Improve error handling
- Add descriptive comments
- Follow naming conventions from .claude/CONVENTIONS.md
- Include example usage in comments

Provide before/after comparison with explanation of improvements.
```

### Prompt: Add Comprehensive Error Handling
```
Add production-grade error handling to the sensor reading code.

Requirements:
1. Detect sensor failures (NaN, out-of-range, communication errors)
2. Implement retry logic with exponential backoff
3. Log errors to Serial for debugging
4. Continue operation with last known good values
5. Track error counts and report via MQTT if persistent
6. Add sensor health status indicators

Follow error handling patterns from .claude/CONVENTIONS.md

Provide complete implementation with detailed comments explaining error conditions.
```

---

## Feature Implementation

### Prompt: Add Relay Control for Cooling Fans ✅ IMPLEMENTED
```
[STATUS: IMPLEMENTED in v1.5.0]

This feature is now implemented with the following hardware configuration:

Hardware (from config.h):
- OneWire Bus: GPIO 4 (DS18B20 sensors, 4.7k pull-up to 3.3V)
- DHT22: GPIO 5 (10k pull-up to 3.3V)
- Relay 0: GPIO 15 (Fan0 - MPPT_W)
- Relay 1: GPIO 16 (Fan1 - MPPT_E)
- Relay 2: GPIO 17 (Fan2 - MultiPlus1)
- Relay 3: GPIO 18 (Fan3 - Batteries)
- Control: Active LOW (LOW = relay on, FAN_ACTIVE_LOW=true)
- Button: GPIO 6 (INPUT_PULLDOWN for touch input)

Implemented features:
✅ Read sensors every 2 seconds (SENSOR_INTERVAL in config.h)
✅ Display temperatures with device names from config.h
✅ Display ambient temp/humidity with icons
✅ Hysteresis-based fan control (5°C hysteresis)
✅ MQTT publishing: solar/<device>/temperature, solar/<device>/fan
✅ Button short press (<1s): Test all fans for 5 seconds
✅ Button long press (>5s): Enter sensor assignment mode
✅ Auto-enter assignment mode if no addresses stored
✅ Sensor addresses persisted in NVS
✅ Medium press (1-5s) in assignment mode: Skip to next sensor

MQTT Topics (current):
- solar/mppt_w/temperature, solar/mppt_w/fan
- solar/mppt_e/temperature, solar/mppt_e/fan
- solar/multiplus_1/temperature, solar/multiplus_1/fan
- solar/batteries/temperature, solar/batteries/fan
- solar/ambient/temperature, solar/ambient/humidity
```

### Prompt: Add Web Interface ✅ IMPLEMENTED
```
[STATUS: IMPLEMENTED in v1.5.0]

The web interface is now fully implemented with all requested features:

Implemented features:
✅ Auto-refresh every 5 seconds
✅ Color-coded sensor readings (Green/Yellow/Red based on thresholds)
✅ Fan status indicators (Blue = ON, Grey = OFF)
✅ WiFi RSSI with color-coded signal strength indicator
✅ MQTT connection status (Green/Red dot)
✅ Last update timestamp
✅ Firmware version display
✅ Professional CSS styling (dark theme)
✅ "Test Fans" button - triggers 5-second fan test
✅ "Firmware Update" button - shows upload form
✅ OTA firmware upload - POST to /update with .bin file
✅ Uptime, free heap, and boot count in footer

Endpoints:
- GET  /           - Main dashboard
- GET  /test-fans  - Trigger fan test sequence
- POST /update     - OTA firmware upload (multipart form)

Libraries required:
- ESP Async WebServer
- Async TCP

Access at: http://<device-ip>/
```

### Prompt: Implement MQTT Autodiscovery
```
Add Home Assistant MQTT autodiscovery so sensors appear automatically without manual configuration.

Requirements:
1. Publish discovery messages for all sensors:
   - Temperature values 1-4
   - Fan statusses 1-4
   - Ambient Temperature
   - Ambient humidity
2. Follow Home Assistant discovery format:
   - Topic: homeassistant/sensor/[device]/[sensor]/config
   - Include device information
   - Set appropriate device classes
3. Publish discovery on boot and every 24 hours
4. Include unique IDs and proper naming

Reference: https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery

Provide:
- Complete discovery message generation
- JSON payload examples
- Function to publish all discovery messages
- Integration in setup() and loop()
```

### Prompt: Add OTA Firmware Updates ✅ IMPLEMENTED
```
[STATUS: IMPLEMENTED in v1.5.0]

OTA firmware updates are now implemented via the web interface:

Implemented features:
✅ Web-based OTA upload via /update endpoint
✅ File upload with .bin firmware files
✅ Progress feedback in web UI
✅ Success/failure status page after upload
✅ Automatic reboot after successful update
✅ Error handling with descriptive messages
✅ Page auto-redirects to dashboard after reboot

How to use:
1. Open http://<device-ip>/ in browser
2. Click "Firmware Update" button
3. Select compiled .bin file
4. Click "Upload & Install"
5. Wait for success message and automatic reboot
6. Page redirects to dashboard after 10 seconds

Technical implementation:
- Uses ESPAsyncWebServer with Update library
- Chunked upload handling for large files
- No password protection (internal network only)

To compile firmware for upload:
arduino-cli compile --fqbn esp32:esp32:esp32s3 --output-dir ./build "D:/Projects/VTM/ESP32_Victron_Dual_DS1820_MQTT_7735/ESP32_Victron_Dual_DS1820_MQTT_7735_v2"
# Upload the .bin file from ./build/
```

---

## Testing and Validation

### Prompt: Create Test Mode
```
Create a test mode that simulates temperature ranges without physical sensors for validation.

Requirements:
1. Activate test mode via Serial command: "TEST ON"
2. Cycle through temperature ranges:
   - Normal: 20-30°C
   - Warning: 40-45°C
   - Critical: 50-65°C
3. Display current test value on screen
4. Publish test values to MQTT with "TEST:" prefix
5. Allow manual temperature setting via Serial
6. Exit test mode: "TEST OFF"
7  Add functionality to the web pages to enble this test

Use cases:
- Verify display color changes at thresholds
- Test MQTT publishing
- Validate fan activation logic (when implemented)
- Demonstrate system without physical hardware

Provide complete test mode implementation with Serial command parser and websit functionality.
```

### Prompt: Add Diagnostic Information Output
```
Create a diagnostic function that outputs complete system information for troubleshooting.

Output to Serial:
1. Firmware version and compile date
2. Hardware info:
   - Board type
   - CPU frequency
   - Flash size
   - Free heap/min heap
3. Network info:
   - WiFi SSID
   - IP address
   - MAC address
   - RSSI signal strength
4. MQTT info:
   - Broker IP
   - Connection status
   - Last publish time
5. Sensor info:
   - DS18B20 addresses detected
   - Last sensor readings
   - DHT22 status
6. Display info:
   - Current screen number
   - Last update time

Trigger: Serial command "DIAG" or button press

Format output as readable table with clear sections.
```

### Prompt: Add Memory Leak Detection
```
Implement memory monitoring to detect leaks during long-term operation.

Requirements:
1. Track heap usage every minute
2. Log if free heap decreases consistently
3. Publish heap stats to MQTT:
   - victron/monitor/heap_free
   - victron/monitor/heap_min
4. Alert via Serial if free heap drops below 20KB
5. Track largest free block
6. Add heap statistics to diagnostic output

Implementation:
- Non-blocking timing
- Rolling average over 10 samples
- Detect downward trend (leak indicator)
- Graph display option on TFT

Provide complete monitoring implementation with trend detection.
```

---

## Optimization

### Prompt: Optimize Display Performance
```
Optimize the display update code to reduce flicker and improve responsiveness.

Current issues:
- Full screen clears cause visible flicker
- Text updates are slow
- Screen transitions are jarring

Requirements:
1. Use partial updates (only redraw changed regions)
2. Implement dirty rectangle tracking
3. Buffer text before displaying to calculate exact dimensions
4. Add smooth transitions between screens (optional fade)
5. Optimize font rendering (cache common characters if possible)

Constraints:
- Maintain current functionality
- Keep memory usage reasonable
- No external libraries if possible

Provide optimized display code with performance comparison comments.
```

### Prompt: Reduce Power Consumption
```
Implement power saving features for battery-backed operation.

Requirements:
1. Reduce WiFi transmit power when RSSI is strong

```

### Prompt: Optimize Memory Usage
```
Analyze and reduce memory footprint of the application.

Tasks:
1. Identify largest RAM users (run compilation with verbose memory map)
2. Move const data to PROGMEM (Flash instead of RAM)
3. Replace String objects with char arrays where possible
4. Optimize buffer sizes (not too large, not too small)
5. Reduce global variable scope where possible

Provide:
1. Memory usage before and after
2. List of optimizations applied
3. Trade-offs (speed vs. memory)
4. Recommendations for further reduction if needed

Target: Reduce dynamic memory usage by at least 20%
```

---

## Debugging and Diagnostics

### Prompt: Add Structured Debug Logging
```
Implement a debug logging system with severity levels.

Requirements:
1. Log levels: ERROR, WARN, INFO, DEBUG, VERBOSE
2. Compile-time configuration:
   #define DEBUG_LEVEL 2  // 0=none, 1=error, 2=warn, 3=info, 4=debug, 5=verbose
3. Macros for easy logging:
   LOG_ERROR("message");
   LOG_INFO("value: %d", value);
4. Include timestamp (millis()) in logs
5. Optional: Include source file and line number
6. Format: [LEVEL][timestamp] message

Example usage:
```c
LOG_ERROR("Sensor read failed: timeout");
LOG_WARN("Low WiFi signal: %d dBm", rssi);
LOG_INFO("MQTT published: topic=%s value=%.2f", topic, value);
LOG_DEBUG("Entering loop iteration %d", count);
```

Provide complete logging system with minimal overhead when disabled.
```

### Prompt: Add Watchdog Timer
```
Implement watchdog timer to automatically recover from hangs or crashes.

Requirements:
1. Use ESP32 hardware watchdog
2. Timeout: 30 seconds (adjust if needed)
3. Feed watchdog in loop() only when:
   - WiFi is connected
   - MQTT is publishing
   - Sensors are reading correctly
4. On watchdog reset:
   - Log reset reason to EEPROM/preferences
   - Publish reset count to MQTT on next boot
5. Allow graceful restart command via MQTT

Implementation:
- Add watchdog initialization in setup()
- Strategic watchdog feeding points
- Reset counter persistence
- Boot reason detection and logging

Provide complete watchdog implementation with reset tracking.
```

### Prompt: Add Remote Serial Console via Telnet
```
Implement telnet access to Serial output for remote debugging.

Requirements:
1. Telnet server on port 23
2. Mirror all Serial output to telnet clients
3. Accept Serial commands via telnet
4. Support multiple concurrent clients (max 3)
5. Add authentication (password protection)
6. Commands:
   - "status" - show system status
   - "restart" - reboot device
   - "wifi" - show WiFi info
   - "mqtt" - show MQTT info

Security:
- Timeout inactive connections after 5 minutes
- Rate limit connection attempts
- Configurable enable/disable

Provide complete telnet server with command parser.
```

---

## Code Migration and Updates

### Prompt: Migrate from AsyncMqttClient to ESP32MQTTClient
```
The current sketch uses AsyncMqttClient but should use ESP32MQTTClient library.

Tasks:
1. Remove AsyncMqttClient includes and timer includes
2. Add ESP32MQTTClient include
3. Update client declaration:
   - From: AsyncMqttClient mqttClient;
   - To: ESP32MQTTClient mqttClient;
4. Update connection setup:
   - Use setURI() and setCredentials()
   - Remove timer-based reconnection
5. Update callback functions to match new library
6. Update publish calls to new API format
7. Test all MQTT functionality

Provide:
- Complete updated MQTT section
- Before/after comparison
- Migration notes for any behavior changes
- Updated library installation in install_libraries.ps1
```

### Prompt: Add Configuration File Support
```
Move hardcoded configuration to a separate config.h file.

Requirements:
1. Create config..h as template
2. Create .gitignore to exclude config.h
3. Move these to config.h:
   - WiFi credentials
   - MQTT broker settings
   - MQTT topics
   - Display Names for each Temp sensor 1-4, E.g.  MPPT_West: 10.1°C  
   - Sensor addresses
   - GPIO pin definitions
   - Temperature thresholds
   - Timing intervals
4. Add compile-time validation (check required defines exist)
5. Add instructions in README for setup
6. Add option to donwload new Conifg .h from webpage and reboot

Structure:
```c
// config.h
#ifndef CONFIG_H
#define CONFIG_H

// WiFi Settings
#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"

// MQTT Settings
#define MQTT_BROKER "192.168.40.250"
// ... etc

#endif
```

Provide complete config.h template and main sketch modifications.
```

### Prompt: Update to Latest Arduino ESP32 Core
```
Update code for compatibility with Arduino ESP32 Core 3.x.

Research and implement:
1. Check for API changes in:
   - WiFi library
   - SPI library
   - Wire library
2. Update deprecated function calls
3. Test compilation with latest core
4. Update any breaking changes
5. Document required core version

Provide:
- List of changes needed
- Updated code sections
- Minimum required core version
- Benefits of upgrading
```

---

## Documentation

### Prompt: Generate API Documentation
```
Create comprehensive inline documentation for all public functions.

Format: Doxygen-style comments
```c
/**
 * @brief Read temperature from specific DS18B20 sensor
 * 
 * Requests temperature conversion and reads the result from the specified
 * sensor. Includes error checking for invalid readings.
 * 
 * @param sensor Device address of DS18B20 to read (8-byte array)
 * @return float Temperature in Celsius, or -127.0 on error
 * 
 * @note Blocks for ~750ms during conversion
 * @warning Returns -127°C if sensor not found or communication fails
 * 
 * @example
 * DeviceAddress sensor1 = {0x28, 0xD6, ...};
 * float temp = getTemperatures(sensor1);
 * if (temp != -127.0) {
 *   Serial.printf("Temperature: %.2f°C\n", temp);
 * }
 */
float getTemperatures(DeviceAddress sensor);
```

Document all functions in the sketch with:
- Brief description
- Parameter descriptions
- Return value description
- Important notes
- Usage examples
```

### Prompt: Create Troubleshooting Decision Tree
```
Create a troubleshooting flowchart in Markdown for common issues.

Format:
```
## WiFi Not Connecting
1. Is the LED blinking?
   - YES → Go to step 2
   - NO → Check power supply
2. Is SSID correct in config?
   - YES → Go to step 3
   - NO → Update config.h and recompile
3. Check Serial output for error
   - "WL_NO_SSID_AVAIL" → Wrong SSID or 5GHz network
   - "WL_CONNECT_FAILED" → Wrong password
   ...
```

Cover these scenarios:
- WiFi connection issues
- MQTT not publishing
- Sensors reading incorrectly
- Display not working
- Memory crashes
- Reset loops

Provide decision tree with specific error messages and solutions.
```

---

## Production Deployment

### Prompt: Create Production Checklist
```
Generate a pre-deployment checklist for production firmware.

Include:
1. Code quality checks
   - [ ] No debug Serial.print() in hot paths
   - [ ] All sensors have error handling
   - [ ] Watchdog timer configured
   - [ ] Memory usage within limits
2. Configuration checks
   - [ ] Production WiFi credentials
   - [ ] Production MQTT broker settings
   - [ ] Correct sensor addresses
   - [ ] Appropriate timing intervals
3. Testing checklist
   - [ ] 24-hour stability test passed
   - [ ] WiFi reconnection tested
   - [ ] MQTT reconnection tested
   - [ ] All sensors reading correctly
   - [ ] Display functioning properly
4. Documentation checks
   - [ ] Version number updated
   - [ ] CHANGELOG.md updated
   - [ ] README.md up to date

Format as interactive Markdown checklist with detailed sub-items.
```

### Prompt: Add Version Management
```
Implement proper version tracking and build information.

Requirements:
1. Define version in code:
   ```c
   #define FW_VERSION "2.1.0"
   #define BUILD_DATE __DATE__
   #define BUILD_TIME __TIME__
   ```
2. Display version on boot (Serial and display)
3. Publish version to MQTT on boot:
   - victron/monitor/version
   - victron/monitor/build_date
4. Add version to diagnostic output
5. Include git commit hash if possible (compile-time)

Follow semantic versioning (MAJOR.MINOR.PATCH)

Provide complete version management implementation.
```

### Prompt: Create Automated Build Script
```
Create a PowerShell script for automated production builds.

Script should:
1. Verify all required libraries installed
2. Check config.h exists (not config.example.h)
3. Compile firmware
4. Run size checks (warn if >80% flash/RAM)
5. Generate build report:
   - Firmware version
   - Build timestamp
   - Memory usage stats
   - Compiler version
6. Create output folder with:
   - Compiled .bin file
   - Build report
   - Config file (sanitized - no passwords)
7. Optional: Upload via OTA to test device

Script location: build-production.ps1

Provide complete automated build script with error checking.
```

---

## Best Practices Prompts

### Prompt: Apply Production Best Practices
```
Review entire codebase and apply production firmware best practices.

Check and implement:
1. **Initialization**
   - All hardware initialized before use
   - Proper error checking on initialization
   - Reasonable startup delays for hardware

2. **Error Handling**
   - All sensor reads checked for validity
   - Network failures handled gracefully
   - No unchecked return values

3. **Resource Management**
   - No memory leaks
   - Proper cleanup on errors
   - Bounded buffer usage

4. **Timing**
   - No delay() calls in loop()
   - All timing using millis()
   - Rollover handling correct

5. **Watchdog**
   - Watchdog timer implemented
   - Fed regularly during normal operation
   - Not fed during error conditions

6. **Logging**
   - Appropriate debug levels
   - Helpful error messages
   - Performance-critical sections not logging

7. **Configuration**
   - Magic numbers replaced with named constants
   - Configurable values in config.h
   - Sane default values

Provide comprehensive review with specific improvements for each area.
```

---

## Contextual Information for Claude

When using these prompts, always provide Claude Code with:
1. Current code context (the .ino file)
2. Hardware configuration (.claude/HARDWARE.md if exists)
3. Coding conventions (.claude/CONVENTIONS.md if exists)
4. Current issues or limitations you're facing

### Example Complete Prompt
```
I need to add relay control for cooling fans to the ESP32 Victron Temperature Monitor.

**Current Context:**
- Read .claude/PROJECT.md for full project overview
- Read .claude/HARDWARE.md for pinout
- Read .claude/CONVENTIONS.md for coding standards
- Main sketch: VictronTempMonitor.ino

**Hardware:**
- Relay 1: GPIO 25 (MPPT cooling fan)
- Relay 2: GPIO 26 (MultiPlus cooling fan)
- Control type: Active HIGH

**Requirements:**
[paste requirements from prompt above]

**Constraints:**
- Must not interfere with existing sensor reading or display code
- Must be non-blocking
- Must integrate with existing MQTT publishing

Please implement complete solution following project conventions.
```

---

**Last Updated:** 2026-01-31
**Firmware Version:** 1.7.7
**Board:** ESP32-S3-DevKitC-1
**For:** ESP32-S3 Arduino-based Production Firmware Development
