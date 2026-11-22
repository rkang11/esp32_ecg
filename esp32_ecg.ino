// ESP32 HW-724 / Lolin-style OLED + AD8232 educational ECG.
// Move LO- to GPIO13: GPIO4 is reserved for the built-in OLED.
// Disconnect electrodes before USB programming. Use standalone battery power
// while electrodes are attached. Not a medical or diagnostic instrument.
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include "EcgProcessing.h"

constexpr float MAINS_HZ = 60; // Change to 50 in 50 Hz regions.
EcgProcessing processing(MAINS_HZ);

constexpr int ECG_PIN = 14, LO_PLUS = 16, LO_MINUS = 13;
constexpr int OLED_SDA = 5, OLED_SCL = 4;
constexpr int WIDTH = 128, HEIGHT = 64;
Adafruit_SSD1306 display(WIDTH, HEIGHT, &Wire, -1);
portMUX_TYPE sharedMux = portMUX_INITIALIZER_UNLOCKED;
int16_t trace[WIDTH] = {};
unsigned writeIndex = 0;
float shownBpm = 0;
bool disconnected = true, settling = true;

// The OLED runs on core 0 so a full I2C frame does not pause ADC sampling.
void displayTask(void *) {
  for (;;) {
    int16_t points[WIDTH];
    float rate;
    bool off, warmup;
    portENTER_CRITICAL(&sharedMux);
    for (int i = 0; i < WIDTH; ++i) points[i] = trace[(writeIndex + i) % WIDTH];
    rate = shownBpm; off = disconnected; warmup = settling;
    portEXIT_CRITICAL(&sharedMux);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("ECG   BPM ");
    if (rate > 0 && !off && !warmup) display.print((int)roundf(rate));
    else display.print("--");
    display.setCursor(0, 10);
    if (off) display.print("CHECK ELECTRODES");
    else if (warmup) display.print("SETTLING...");
    display.drawFastHLine(0, 46, WIDTH, SSD1306_WHITE);
    if (!off && !warmup) {
      for (int x = 1; x < WIDTH; ++x) {
        int y0 = constrain(46 - points[x - 1] / 40, 30, 63);
        int y1 = constrain(46 - points[x] / 40, 30, 63);
        display.drawLine(x - 1, y0, x, y1, SSD1306_WHITE);
      }
    }
    display.display();
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LO_PLUS, INPUT);
  pinMode(LO_MINUS, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(ECG_PIN, ADC_11db);
  // GPIO14 is ADC2: keep WiFi disabled with this wiring.
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setTimeOut(20);
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false)) {
    if (xTaskCreatePinnedToCore(displayTask, "OLED", 4096, nullptr, 1, nullptr, 0) != pdPASS)
      Serial.println("OLED task failed; serial output remains available.");
  } else Serial.println("OLED initialization failed; check model/address/pins.");
}

void loop() {
  static uint32_t nextSample = micros();
  static uint32_t connectedAt = 0;
  static bool wasOff = true;
  static unsigned decimation = 0;
  uint32_t now = micros();
  if ((int32_t)(now - nextSample) < 0) { delay(1); return; }
  nextSample += 4000; // 250 samples per second; no burst catch-up after a stall.
  if ((int32_t)(now - nextSample) >= 0) nextSample = now + 4000;
  uint32_t ms = millis();
  bool plus = digitalRead(LO_PLUS) == HIGH;
  bool minus = digitalRead(LO_MINUS) == HIGH;
  bool off = plus || minus;
  int raw = analogRead(ECG_PIN);
  if (off || wasOff) {
    processing.reset(raw, ms);
    connectedAt = ms;
  }
  bool warmup = off || ms - connectedAt < 2000;
  wasOff = off;
  bool peak = processing.step(raw, ms);
  float filtered = processing.clean;
  float rate = processing.bpm;
  portENTER_CRITICAL(&sharedMux);
  disconnected = off; settling = warmup; shownBpm = rate;
  if (++decimation == 4) { // approximately two seconds across the screen
    decimation = 0;
    trace[writeIndex] = warmup ? 0 : (int16_t)filtered;
    writeIndex = (writeIndex + 1) % WIDTH;
  }
  portEXIT_CRITICAL(&sharedMux);
  // Numeric-only fields for Arduino Serial Plotter; skip if USB is congested.
  char line[128];
  int count = snprintf(line, sizeof(line), "ECG:%.1f,BPM:%.1f,Peak:%d,LeadsOff:%d\n",
                       warmup ? 0.0f : filtered, rate, peak ? 300 : 0, off ? 1 : 0);
  if (count > 0 && count < (int)sizeof(line) && Serial.availableForWrite() >= count)
    Serial.write((const uint8_t *)line, count);
}
