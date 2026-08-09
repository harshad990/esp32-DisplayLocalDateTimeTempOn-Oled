# ESP32 LM35 Temperature Display

![CI](https://github.com/harshad990/esp32-lm35-temp/actions/workflows/platformio-ci.yml/badge.svg)
![](https://img.shields.io/github/v/release/harshad990/esp32-lm35-temp)

This PlatformIO project reads temperature from an LM35 sensor and displays it
on a 128x64 SSD1306 OLED connected to an ESP32 development board.

The sketch measures the LM35 analog output on ADC pin GPIO34, converts the
sensor voltage to Celsius, computes Fahrenheit, and shows both current and
smoothed temperature values on the OLED screen.

## Features

- LM35 temperature reading on ESP32 ADC pin GPIO34
- OLED display with current temperature in °C and °F
- Rolling average smoothing across 8 samples
- Refreshes every 1 second

## Wiring

### LM35 to ESP32

| LM35 pin | ESP32 pin |
|----------|-----------|
| VCC      | 3.3V      |
| GND      | GND       |
| Vout     | GPIO34    |

### SSD1306 OLED to ESP32

| OLED pin | ESP32 pin |
|----------|-----------|
| VCC      | 3.3V      |
| GND      | GND       |
| SDA      | GPIO21    |
| SCL      | GPIO22    |

The OLED is configured for the I2C address `0x3C`.

## Build and Upload

Use PlatformIO from the project root:

```sh
pio run
pio run --target upload
```

## Release

This repository is tagged at `v1.0.0` for the initial stable snapshot. See the
GitHub Releases page for release notes.

## Notes

- The ESP32 ADC is configured for 12-bit resolution and 11dB attenuation.
- Ensure the LM35 is powered from 3.3V, not 5V, when using the ESP32.
- If the OLED does not display, verify I2C wiring and confirm the display address.

