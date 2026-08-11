# THOMAS-Arduino-Uno

Temperature and humidity logger

## Table of Contents
- [About](#about)
- [Wiring and Hardware](#wiring-and-hardware)
- [Usage](#usage)


## About

This sensor was created for my AP Bio class. The name, THOMAS, was chosen as a class, standing for Temperature & Humidity Observation/Monitoring Assembled Sensor.

THOMAS is a very user-friendly data-logger and sensor that saves the temperature and humidity over time for multi-day projects. This data is saved to a microSD card in a format that is easy to use in graphs through importing to programs such as Google Sheets.

## Wiring and Hardware

```
DHT22
    VCC -> 5V
    GND -> GND
    DATA -> D2

SD Card Module
    VCC -> 5V
    GND -> GND
    CS -> D10
    MOSI -> D11
    MISO -> D12
    SCK -> D13

  RTC (DS3231)
    VCC -> 5V
    GND -> GND
    SDA -> A4
    SCL -> A5

  LCD1602 I²C
    VCC -> 5V
    GND -> GND
    SDA -> A4
    SCL -> A5

  Push Button
    VCC -> 5V
    OUTPUT -> D7

  LED (red preferably)
    GND -> GND
    VCC -> D4
```


  A way to read and write to an SD card from a computer will be necessary to pass data to/fro THOMAS efficiently. If on a Mac, purchase an SD card reader for your computer.
  

## Usage

  To get it to run, first download the Arduino IDE. Then, load the code into the IDE. Connect an Arduino-Uno with the above hardware and wiring. Compile the code and upload it to the device.
  
  When you start the program, you will be prompted on the digital display to insert an SD card. Once inserted, and a button is pressed to start, the temperature starts to log. Once finished, press the button to access the menu, scroll to the "off" button, and click it. You can then scan that microSD from a computer, find the correct .txt file, and import it into a program such as Google Sheets or Excel.



  
