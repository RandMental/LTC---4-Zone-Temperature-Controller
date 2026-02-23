# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 Arduino project for monitoring Victron solar equipment temperatures. Monitors 4x DS18B20 sensors (MPPT, MultiPlus, Inverter, Battery), controls 4x cooling fans via relays, publishes to MQTT, and displays on ILI9342 3.2" 240x320 TFT. Supports runtime configuration via SD card INI file.

## Build Environment

- **Platform**: Arduino IDE or PlatformIO
- **Board**: ESP32-S3-DevKitC-1 (44-pin)
- **FQBN**: esp32:esp32:esp32s3
- **Library**: ESP32MQTTClient (github.com/cyijun/ESP32MQTTClient)

## Required Libraries

```
arduino-cli lib install "OneWire"
arduino-cli lib install "DallasTemperature"
arduino-cli lib install "Adafruit GFX Library"
arduino-cli lib install "Adafruit BusIO"
arduino-cli lib install "Adafruit ST7735 and ST7789 Library"
arduino-cli lib install "Adafruit ILI9341"
arduino-cli lib install "DHT sensor library"
arduino-cli lib install "Adafruit Unified Sensor"
arduino-cli lib install "ESP32MQTTClient"
arduino-cli lib install "ESP Async WebServer"
arduino-cli lib install "Async TCP"
```

Note: `SD.h` is built into the ESP32 Arduino core (no extra install needed).

## Hardware Configuration (ESP32-S3-DevKitC-1)

| Component | GPIO Pin | Notes |
|-----------|----------|-------|
| DS18B20 OneWire bus | 4 | Safe pin; 4.7k pull-up to 3.3V |
| DHT22 sensor  | 5 | Safe pin; 10k pull-up to 3.3V |
| SD Card CS | 6 | Shares SPI2 bus with TFT and touchscreen |
| Touch IRQ | 7 | XPT2046 interrupt (low when touched) |
| TFT RST       | 8 | Safe pin |
| TFT DC        | 9 | Safe pin |
| TFT CS        | 10 | SPI2 default SS |
| SPI MOSI | 11 | SPI2 default MOSI |
| SPI SCLK | 12 | SPI2 default SCK |
| SPI MISO | 13 | SPI2 default MISO (touch readback + SD) |
| Touch CS | 14 | XPT2046 chip select |
| Fan Relay 0 (MPPT_W) | 15 | Safe pin; active LOW (FAN_ACTIVE_LOW=true) |
| Fan Relay 1 (MPPT_E) | 16 | Safe pin; active LOW |
| Fan Relay 2 (MultiPlus1) | 17 | Safe pin; active LOW |
| Fan Relay 3 (Batteries) | 18 | Safe pin; active LOW |
| RGB LED | 48 | WS2812 NeoPixel (DISABLED) |

### SPI2 Bus Sharing
Three devices share the SPI2 bus (MOSI=11, MISO=13, SCLK=12):
- **TFT Display** (ILI9342): CS=GPIO10
- **Touchscreen** (XPT2046): CS=GPIO14
- **SD Card**: CS=GPIO6

All CS pins are deselected HIGH at boot before SPI init.

### ESP32-S3 GPIO Safety
```
Strapping pins (avoid): GPIO 0, 3, 45, 46
USB pins (avoid): GPIO 19, 20
JTAG pins (avoid): GPIO 39-42
UART0 (serial): GPIO 43, 44
Octal PSRAM (if equipped): GPIO 35, 36, 37
Safe GPIOs used: 4-18 (all safe on S3)
```

## Architecture

### Runtime Configuration (RuntimeConfig struct)
All configuration lives in a global `cfg` struct populated at boot:
1. **Compiled defaults** from `config.h` #defines (via `initConfigDefaults()`)
2. **SD card overrides** from `/config.ini` (via `loadConfigFromSD()`)
3. **MQTT topics** built from `cfg.mqttBaseTopic` (via `buildMqttTopics()`)

No recompilation needed to change WiFi, MQTT, thresholds, device names, or timing.

### Sensor Address Priority
Three-tier fallback for DS18B20 addresses:
1. **NVS** (from sensor assignment mode) - highest priority
2. **SD card** config.ini `sensor_addr_*` keys
3. **Compiled defaults** from config.h

### Timing (Non-Blocking, Runtime-Configurable)
| Config Key | Default | Purpose |
|------------|---------|---------|
| sensor_interval | 2000ms | Read all sensors |
| display_interval | 4000ms | Cycle display screens |
| mqtt_interval | 30000ms | Publish telemetry |
| status_interval | 60000ms | Publish heap/uptime |
| ntp_interval | 7200000ms | Sync time |

### Reliability Features
- **Watchdog**: 60s hardware watchdog timer
- **Sensor Filtering**: 5-sample moving average
- **Error Detection**: 3 consecutive errors marks sensor failed
- **Failsafe Fan Control**: Sensor errors force fan ON; configured sensors start ON until valid reading
- **WiFi Reconnection**: Exponential backoff (30s -> 5min max)
- **MQTT LWT**: "offline" published on disconnect
- **Heap Monitoring**: Warns below 20KB
- **Web Interface**: http://device-ip/ for monitoring, OTA updates, and sensor assignment
- **Sensor Assignment Mode**: Touch-activated mode to assign DS18B20 addresses with NVS persistence
- **SD Card Config**: Optional runtime config without recompilation

### Boot Sequence
```
1. Serial + banner
2. initConfigDefaults()        - cfg from compiled #defines
3. state.init(), loadPreferences()
4. Deselect SPI CS pins HIGH   - TFT_CS, TCH_CS, SD_CS_PIN
5. initWatchdog()
6. initDisplay()               - SPI.begin, tft.begin (hardware only)
7. initTouch()
8. initSDCard()                - Mount SD on shared SPI bus
9. loadConfigFromSD()          - Override cfg from /config.ini
10. buildMqttTopics()          - Build topic strings from cfg.mqttBaseTopic
11. Set fanOnState/fanOffState - From cfg.fanActiveLow
12. tft.setRotation()          - Re-apply if SD changed it
13. initRelays()               - Uses runtime fan polarity
14. loadSensorAddresses()      - NVS > SD card > compiled defaults
15. displayStartupScreen()     - Shows SD card status
16. Auto-enter assignment if no valid addresses
17. initSensors(), initWiFi(), initNTP()
18. Prime sensor readings, clear screen
```

## SD Card Configuration

Place a `config.ini` file on the SD card root. Only include keys you want to override.
See the `config.ini` template file in the project directory for all supported keys.

Key format: `key = value` (one per line). Comments start with `;` or `#`.

## MQTT Topics

### Telemetry (per device)
- `solar/<device>/temperature` - Filtered temperature
- `solar/<device>/fan` - ON/OFF

### Ambient
- `solar/ambient/temperature`
- `solar/ambient/humidity`

### Status
- `solar/monitor/status` - online/offline (LWT)
- `solar/monitor/version` - Firmware version
- `solar/monitor/uptime` - Seconds since boot
- `solar/monitor/rssi` - WiFi signal strength
- `solar/monitor/heap_free` - Free heap bytes

## Web Interface

The device serves a web interface on port 80 with:
- Real-time sensor readings with color-coded status
- Fan status indicators (ON = blue, OFF = grey)
- WiFi signal strength and MQTT connection status
- Test Fans button (triggers fan test sequence)
- Assign Sensors button (enters sensor assignment mode)
- OTA Firmware Update (upload .bin file)
- Auto-refresh every 5 seconds

Endpoints:
- `/` - Main dashboard
- `/test-fans` - Trigger fan test
- `/assign-sensors` - Enter sensor assignment mode
- `/save-config` - Save current config to SD card as /config.ini
- `/update` - Firmware upload (POST with multipart form)

## Debug Levels

Set `DEBUG_LEVEL` (0-4) to control logging:
- 0: None
- 1: ERROR only
- 2: ERROR + WARN
- 3: ERROR + WARN + INFO (default)
- 4: All including DEBUG

## Adding New Sensors

1. Run the OneWire scanner (code in file footer) or use assignment mode
2. Add address to `config.h` SENSOR_ADDR_* or SD card config.ini `sensor_addr_*`
3. Add name to config.h DEVICE_NAME_* or SD card `device_name_*`
4. Add topic suffix to config.h DEVICE_TOPIC_* or SD card `device_topic_*`
5. Add thresholds to config.h TEMP_FAN_ON/OFF_* or SD card `temp_fan_on/off_*`
6. Add relay pin to `FAN_RELAY_PINS[]`
7. Update `NUM_TEMP_SENSORS` and `NUM_FANS`

## Key Files

- `*.ino` - Main firmware
- `config.h` - Compiled defaults (all #defines)
- `config.ini` - SD card runtime config template
- `README.md` - Full project context and patterns
- `PROMPTS.md` - Example development prompts
- `USAGE_GUIDE.md` - How to use with Claude Code
