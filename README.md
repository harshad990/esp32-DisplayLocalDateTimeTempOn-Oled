# ESP32 SSD1306 Local Clock

This PlatformIO project displays a time-based greeting, the local time, date,
and current London, Ontario temperature on a 128x64 SSD1306 OLED. Time is
synchronized over Wi-Fi using NTP and is configured for the `America/Toronto`
timezone, including daylight-saving time. Weather data comes from Open-Meteo
and refreshes every 15 minutes.

## Wiring

| SSD1306 | ESP32 |
|---------|-------|
| VCC     | 3.3 V |
| GND     | GND |
| SDA     | GPIO 21 |
| SCL     | GPIO 22 |

The display is configured for the common I2C address `0x3C`.

## Wi-Fi configuration

Copy `include/secrets.example.h` to `include/secrets.h`, then enter the Wi-Fi
network name and password. The real `secrets.h` file is ignored by Git.

The ESP32 connects automatically at startup and retries every few seconds if
the network is temporarily unavailable. ESP32 boards require a 2.4 GHz Wi-Fi
network. For router security, WPA2 or WPA2/WPA3 compatibility mode is the most
widely compatible choice.

If the OLED reports that the network is visible but connection fails, configure
the router's 2.4 GHz band for WPA2-Personal with AES, make Protected Management
Frames optional, and check that MAC filtering or access control is not blocking
the ESP32.

Build and upload with PlatformIO:

```sh
pio run
pio run --target upload
```

