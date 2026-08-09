#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {
constexpr int kScreenWidth = 128;
constexpr int kScreenHeight = 64;
constexpr int kOledResetPin = -1;
constexpr uint8_t kOledAddress = 0x3C;
constexpr int kSdaPin = 21;
constexpr int kSclPin = 22;
constexpr int kLm35Pin = 34;  // ADC pin connected to LM35 output
constexpr int kTemperatureSmoothingSamples = 8;

Adafruit_SSD1306 display(kScreenWidth, kScreenHeight, &Wire, kOledResetPin);
float temperatureHistory[kTemperatureSmoothingSamples] = {NAN};
float temperatureHistorySum = 0.0f;
int temperatureHistoryIndex = 0;
int temperatureHistoryCount = 0;

void pushTemperature(float temperatureC) {
  if (temperatureHistoryCount < kTemperatureSmoothingSamples) {
    temperatureHistoryCount++;
  } else {
    temperatureHistorySum -= temperatureHistory[temperatureHistoryIndex];
  }

  temperatureHistory[temperatureHistoryIndex] = temperatureC;
  temperatureHistorySum += temperatureC;
  temperatureHistoryIndex = (temperatureHistoryIndex + 1) % kTemperatureSmoothingSamples;
}

float getSmoothedTemperature() {
  if (temperatureHistoryCount == 0) {
    return NAN;
  }
  return temperatureHistorySum / temperatureHistoryCount;
}

float celsiusToFahrenheit(float celsius) {
  return celsius * 9.0f / 5.0f + 32.0f;
}

void drawCenteredText(const char* text, uint8_t textSize, int16_t y) {
  int16_t x1;
  int16_t y1;
  uint16_t width;
  uint16_t height;

  display.setTextSize(textSize);
  display.getTextBounds(text, 0, y, &x1, &y1, &width, &height);
  display.setCursor((kScreenWidth - static_cast<int>(width)) / 2, y);
  display.print(text);
}

void showMessage(const char* line1, const char* line2 = nullptr,
                 const char* line3 = nullptr) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText(line1, 1, 8);

  if (line2 != nullptr) {
    drawCenteredText(line2, 1, 27);
  }
  if (line3 != nullptr) {
    drawCenteredText(line3, 1, 46);
  }

  display.display();
}

float readLm35TemperatureC() {
  const int raw = analogRead(kLm35Pin);
  const float voltage = raw * 3.3f / 4095.0f;
  return voltage * 100.0f;
}

void showTemperature(float temperatureC, float smoothedC, int rawAdc) {
  char line2[32];
  char line3[32];
  const float temperatureF = celsiusToFahrenheit(temperatureC);
  const float smoothedF = celsiusToFahrenheit(smoothedC);

  if (isnan(smoothedC)) {
    snprintf(line2, sizeof(line2), "%.1f C  %.1f F", temperatureC, temperatureF);
    snprintf(line3, sizeof(line3), "ADC: %d", rawAdc);
  } else {
    snprintf(line2, sizeof(line2), "%.1f C  %.1f F", temperatureC, temperatureF);
    snprintf(line3, sizeof(line3), "Avg: %.1f C  %.1f F", smoothedC, smoothedF);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText("LM35 Temperature", 1, 0);
  drawCenteredText(line2, 1, 20);
  drawCenteredText(line3, 1, 40);
  display.display();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Wire.begin(kSdaPin, kSclPin);

  if (!display.begin(SSD1306_SWITCHCAPVCC, kOledAddress)) {
    Serial.println("SSD1306 initialization failed");
    while (true) {
      delay(1000);
    }
  }

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(kLm35Pin, INPUT);

  showMessage("LM35 ready", "ADC pin 34", "Updating every 1 sec");
  delay(1500);
}

void loop() {
  static unsigned long lastUpdate = 0;
  const unsigned long now = millis();

  if (now - lastUpdate >= 1000) {
    lastUpdate = now;

    const int rawValue = analogRead(kLm35Pin);
    const float temperatureC = rawValue * 3.3f / 4095.0f * 100.0f;
    pushTemperature(temperatureC);
    const float smoothedC = getSmoothedTemperature();

    Serial.printf("LM35 raw=%d, temp=%.2f C, avg=%.2f C\n",
                  rawValue, temperatureC, smoothedC);
    showTemperature(temperatureC, smoothedC, rawValue);
  }
}

