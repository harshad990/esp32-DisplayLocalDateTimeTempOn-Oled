#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_wifi.h>
#include <time.h>

#include "secrets.h"

namespace {
constexpr int kScreenWidth = 128;
constexpr int kScreenHeight = 64;
constexpr int kOledResetPin = -1;
constexpr uint8_t kOledAddress = 0x3C;
constexpr int kSdaPin = 21;
constexpr int kSclPin = 22;
constexpr int kLm35Pin = 34;  // Connect LM35 output to GPIO34 (ADC1 channel 6)
constexpr float kLm35AdcReferenceVoltage = 1.1f;  // Use 1.1V full-scale for ADC_0db on ESP32
constexpr float kLm35VoltageToCelsius = 100.0f;  // 10 mV per °C
constexpr float kLm35CalibrationOffset = 19.0f;  // manual offset to center room temperature near 24°C
constexpr float kLm35SmoothingAlpha = 0.25f;  // smoothing factor for LM35 readings

// America/Toronto: UTC-5 in winter and UTC-4 during daylight-saving time.
constexpr char kTimezone[] = "EST5EDT,M3.2.0/2,M11.1.0/2";
constexpr char kWeatherLocation[] = "London ON";
constexpr char kWeatherUrl[] =
    "https://api.open-meteo.com/v1/forecast?latitude=42.98339&"
    "longitude=-81.23304&current=temperature_2m&temperature_unit=celsius&"
    "timezone=America%2FToronto";
constexpr unsigned long kWeatherUpdateIntervalMs = 15UL * 60UL * 1000UL;
constexpr unsigned long kWeatherRetryIntervalMs = 60UL * 1000UL;

Adafruit_SSD1306 display(kScreenWidth, kScreenHeight, &Wire, kOledResetPin);
volatile uint8_t lastWiFiDisconnectReason = 0;
float currentTemperatureC = NAN;
float lm35TemperatureC = NAN;
unsigned long lastWeatherAttempt = 0;

void handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    lastWiFiDisconnectReason = info.wifi_sta_disconnected.reason;
    Serial.printf("WiFi disconnect reason: %u\n", lastWiFiDisconnectReason);
  }
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

struct ConfiguredNetwork {
  bool found = false;
  String ssid;
  int32_t channel = 0;
  int32_t rssi = -127;
  wifi_auth_mode_t security = WIFI_AUTH_OPEN;
  uint8_t bssid[6] = {};
};

String canonicalizeSsid(const String& ssid) {
  String canonical;
  canonical.reserve(ssid.length());

  for (size_t i = 0; i < ssid.length(); ++i) {
    const uint8_t character = static_cast<uint8_t>(ssid[i]);
    if (character >= 'A' && character <= 'Z') {
      canonical += static_cast<char>(character + ('a' - 'A'));
    } else if ((character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9')) {
      canonical += static_cast<char>(character);
    }
  }

  return canonical;
}

ConfiguredNetwork scanConfiguredNetwork() {
  const int networkCount = WiFi.scanNetworks(false, true);
  ConfiguredNetwork network;
  const String configuredCanonical = canonicalizeSsid(WIFI_SSID);

  Serial.printf("WiFi scan found %d networks.\n", networkCount);

  for (int i = 0; i < networkCount; ++i) {
    const String scannedSsid = WiFi.SSID(i);
    if (canonicalizeSsid(scannedSsid) == configuredCanonical &&
        WiFi.RSSI(i) > network.rssi) {
      network.found = true;
      network.ssid = scannedSsid;
      network.channel = WiFi.channel(i);
      network.rssi = WiFi.RSSI(i);
      network.security = WiFi.encryptionType(i);
      memcpy(network.bssid, WiFi.BSSID(i), sizeof(network.bssid));
    }
  }

  if (network.found) {
    Serial.printf(
        "Configured WiFi found as '%s': channel %d, RSSI %d dBm, security %d\n",
        network.ssid.c_str(), network.channel, network.rssi, network.security);
  }

  WiFi.scanDelete();
  return network;
}

void beginCompatibleConnection(const ConfiguredNetwork& network) {
  // Configure first without connecting so WPA2/WPA3 transition settings can
  // be adjusted before the authentication handshake starts.
  WiFi.disconnect(false, false);
  delay(250);
  WiFi.begin(network.ssid.c_str(), WIFI_PASSWORD, network.channel,
             network.bssid, false);
  delay(250);

  wifi_config_t stationConfig;
  if (esp_wifi_get_config(WIFI_IF_STA, &stationConfig) == ESP_OK) {
    stationConfig.sta.pmf_cfg.capable = true;
    stationConfig.sta.pmf_cfg.required = false;
    stationConfig.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    const esp_err_t configResult =
        esp_wifi_set_config(WIFI_IF_STA, &stationConfig);
    Serial.printf("WiFi configuration result: %s\n",
                  esp_err_to_name(configResult));
  }

  delay(250);
  const esp_err_t connectResult = esp_wifi_connect();
  Serial.printf("WiFi connect result: %s\n", esp_err_to_name(connectResult));
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);
  WiFi.onEvent(handleWiFiEvent);

  while (WiFi.status() != WL_CONNECTED) {
    const ConfiguredNetwork network = scanConfiguredNetwork();
    if (!network.found) {
      showMessage("WiFi network missing", WIFI_SSID, "Enable 2.4 GHz");
      delay(5000);
      continue;
    }

    showMessage("Connecting to WiFi", network.ssid.c_str());
    Serial.printf("Connecting to WiFi '%s'...\n", network.ssid.c_str());
    beginCompatibleConnection(network);

    const unsigned long connectionStarted = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - connectionStarted < 20000) {
      delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("WiFi connected. IP address: %s\n",
                    WiFi.localIP().toString().c_str());
      return;
    }

    const wl_status_t connectionStatus = WiFi.status();
    Serial.printf("WiFi connection failed (status %d, reason %u).\n",
                  connectionStatus, lastWiFiDisconnectReason);
    WiFi.disconnect(false, false);
    delay(250);

    showMessage("WiFi connection failed", "Network is visible",
                "Check WPA2/password");

    delay(5000);
  }
}

const char* greetingForHour(int hour) {
  if (hour < 12) {
    return "Good Morning";
  }
  if (hour < 18) {
    return "Good Afternoon";
  }
  return "Good Evening";
}

bool updateTemperature() {
  lastWeatherAttempt = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Weather update skipped: WiFi is disconnected.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(10000);

  if (!http.begin(client, kWeatherUrl)) {
    Serial.println("Weather request setup failed.");
    return false;
  }

  const int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    Serial.printf("Weather request failed with HTTP status %d.\n",
                  responseCode);
    http.end();
    return false;
  }

  JsonDocument weather;
  const DeserializationError error =
      deserializeJson(weather, http.getStream());

  if (error) {
    Serial.printf("Weather JSON error: %s\n", error.c_str());
    http.end();
    return false;
  }

  const float temperature = weather["current"]["temperature_2m"] | NAN;
  http.end();

  if (isnan(temperature)) {
    Serial.println("Weather response did not contain a temperature.");
    return false;
  }

  currentTemperatureC = temperature;
  Serial.printf("%s temperature updated: %.1f C\n", kWeatherLocation,
                currentTemperatureC);
  return true;
}

void drawClock(const tm& localTime) {
  char timeText[9];
  char dateText[18];
  char temperatureText[24];

  strftime(timeText, sizeof(timeText), "%H:%M:%S", &localTime);
  strftime(dateText, sizeof(dateText), "%a, %b %d", &localTime);

  char weatherText[24];
  char lm35Text[24];

  if (isnan(currentTemperatureC)) {
    snprintf(weatherText, sizeof(weatherText), "%s --.- C",
             kWeatherLocation);
  } else {
    snprintf(weatherText, sizeof(weatherText), "%s %.1f C",
             kWeatherLocation, currentTemperatureC);
  }

  if (isnan(lm35TemperatureC)) {
    snprintf(lm35Text, sizeof(lm35Text), "Room --.- C");
  } else {
    snprintf(lm35Text, sizeof(lm35Text), "Room %.1f C", lm35TemperatureC);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText(greetingForHour(localTime.tm_hour), 1, 0);
  drawCenteredText(timeText, 2, 12);
  drawCenteredText(dateText, 1, 34);
  drawCenteredText(lm35Text, 1, 48);
  drawCenteredText(weatherText, 1, 56);
  display.display();
}

}  // namespace

float readLm35TemperatureC() {
  const int rawAdc = analogRead(kLm35Pin);
  const float voltage = rawAdc * (kLm35AdcReferenceVoltage / 4095.0f);
  const float temperature = voltage * kLm35VoltageToCelsius + kLm35CalibrationOffset;
  Serial.printf("LM35: raw=%d, V=%.3f, temp=%.2f C\n", rawAdc, voltage,
                temperature);
  return temperature;
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(kLm35Pin, ADC_0db);
  Wire.begin(kSdaPin, kSclPin);

  if (!display.begin(SSD1306_SWITCHCAPVCC, kOledAddress)) {
    Serial.println("SSD1306 initialization failed");
    while (true) {
      delay(1000);
    }
  }

  showMessage("Connecting to WiFi...");
  connectToWiFi();

  showMessage("Syncing local time...");
  configTzTime(kTimezone, "pool.ntp.org", "time.google.com",
               "time.cloudflare.com");

  lm35TemperatureC = NAN;
  tm localTime;
  if (!getLocalTime(&localTime, 15000)) {
    showMessage("Waiting for time...");
  } else {
    showMessage("Getting weather...");
    updateTemperature();
    lm35TemperatureC = readLm35TemperatureC();
    drawClock(localTime);
  }
}

void loop() {
  static unsigned long lastUpdate = 0;
  const unsigned long now = millis();

  const unsigned long weatherInterval =
      isnan(currentTemperatureC) ? kWeatherRetryIntervalMs
                                 : kWeatherUpdateIntervalMs;
  if (now - lastWeatherAttempt >= weatherInterval) {
    updateTemperature();
  }

  if (now - lastUpdate >= 1000) {
    lastUpdate = now;
    const float latestTemperature = readLm35TemperatureC();
    if (isnan(lm35TemperatureC)) {
      lm35TemperatureC = latestTemperature;
    } else {
      lm35TemperatureC = lm35TemperatureC * (1.0f - kLm35SmoothingAlpha) +
                         latestTemperature * kLm35SmoothingAlpha;
    }

    tm localTime;
    if (getLocalTime(&localTime, 100)) {
      drawClock(localTime);
    } else {
      showMessage("Waiting for time...");
    }
  }
}

