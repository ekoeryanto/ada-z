#ifndef PINS_CONFIG_H
#define PINS_CONFIG_H

// Digital Inputs (3.3V - 24V compatible)
#define DI1_PIN 27
#define DI2_PIN 26
#define DI3_PIN 25
#define DI4_PIN 33

// Analog Inputs (0-10V)
#define AI1_PIN 35
#define AI2_PIN 34
#define AI3_PIN 36

// Analog Current Inputs (4-20mA via ADS1115)
#define A0_CHANNEL 0  // ADS1115 Channel 0
#define A1_CHANNEL 1  // ADS1115 Channel 1

// Digital Outputs
#define DO1_PIN 15
#define DO2_PIN 13
#define DO3_PIN 12
#define DO4_PIN 14

// RS485 pins

// RS485 pins
#define RS485_RX 16
#define RS485_TX 17
#define RS485_DE 4

// SD Card pins (using ESP32 default SPI pins)
#define SD_CS 5      // Default ESP32 CS pin
#define SD_MOSI 23   // Default ESP32 MOSI pin  
#define SD_MISO 19   // Default ESP32 MISO pin
#define SD_SCK 18    // Default ESP32 SCK pin

// I2C pins for ADS1115 and RTC

// I2C pins for ADS1115 and RTC
#define I2C_SDA 21
#define I2C_SCL 22

// ADS1115 Address
#define ADS1115_ADDR 0x48

// TFT Display pins (adjust based on actual hardware)
#define TFT_CS     -1   // Not connected (or set to appropriate pin)
#define TFT_DC     2    // Data/Command pin
#define TFT_RST    -1   // Reset pin (or set to appropriate pin)
#define TFT_MOSI   23   // SPI MOSI (shared with SD card)
#define TFT_SCLK   18   // SPI SCLK (shared with SD card)
#define TFT_MISO   19   // SPI MISO (shared with SD card)

// TFT Backlight control (if available)
#define TFT_BL     -1   // Backlight control pin (set if available)

#endif // PINS_CONFIG_H
