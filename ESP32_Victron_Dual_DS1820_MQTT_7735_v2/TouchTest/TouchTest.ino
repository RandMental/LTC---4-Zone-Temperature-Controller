/*
 * XPT2046 Touchscreen Test Sketch
 *
 * Tests the touchscreen on the ILI9342 3.2" display module.
 * Uses pin definitions from the main project's config.h.
 * Output: Serial console at 115200 baud.
 *
 * Board: ESP32-S3-DevKitC-1
 * FQBN:  esp32:esp32:esp32s3
 */

#include <SPI.h>
#include <XPT2046_Touchscreen.h>

// ---- Pin definitions (from config.h) ----
#define TFT_CS    10   // Display CS  (directly used here since both share the SPI2 bus)
#define TFT_SCLK  12   // SPI2 SCK
#define TFT_MOSI  11   // SPI2 MOSI
#define TFT_MISO  13   // SPI2 MISO
#define TCH_CS    21   // Touch CS
#define TCH_IRQ    7   // Touch IRQ (low when touched)

XPT2046_Touchscreen touch(TCH_CS, TCH_IRQ);

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================");
  Serial.println("  XPT2046 Touchscreen Test");
  Serial.println("  SPI2: SCK=12, MOSI=11, MISO=13");
  Serial.printf("  Touch CS=GPIO%d, IRQ=GPIO%d\n", TCH_CS, TCH_IRQ);
  Serial.println("========================================\n");

  // Deselect the display so it doesn't interfere
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);

  // Init SPI bus (same bus as display)
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TCH_CS);

  // Init touchscreen
  if (touch.begin()) {
    Serial.println("[OK]  Touchscreen initialized");
  } else {
    Serial.println("[FAIL] Touchscreen init failed - check wiring!");
  }
  touch.setRotation(2);  // Match TFT_ROTATION from config.h

  Serial.println("\nTouch the screen to see coordinates...\n");
  Serial.println("  X       Y       Z (pressure)");
  Serial.println("  -----   -----   -----------");
}

void loop() {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();

    Serial.printf("  %-5d   %-5d   %d\n", p.x, p.y, p.z);

    // Wait for finger lift to avoid flooding the console
    while (touch.touched()) {
      delay(10);
    }
    Serial.println("  (released)");
  }

  delay(50);
}
