# Documentation

This docs folder contains the wiring schematic for the ESP32 + LM35 + SSD1306 project.

- `schematic.svg`: connection diagram for the LM35 temperature sensor and the SSD1306 OLED display.

Use the diagram to wire the components correctly:

- LM35 VCC → ESP32 3.3V
- LM35 GND → ESP32 GND
- LM35 Vout → ESP32 GPIO34
- SSD1306 VCC → ESP32 3.3V
- SSD1306 GND → ESP32 GND
- SSD1306 SDA → ESP32 GPIO21
- SSD1306 SCL → ESP32 GPIO22
