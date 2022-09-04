**Intro**

This directory contains the pico-wifi subdirectory, with code which communicates with an ESP8266 WiFi module over UART, and accesses sensor and device functionality. The wemos-wifi subdirectory contains code which connects to the local internet router, establishes a safe connection with an MQTT broker, and sends any changes to topics to the Pico via UART.

The Pico reads the DHT22 sensor for temperature and humidity, and an LDR input off an ADC pin, scaling it for ambient brightness. It also controls the output of a MOSFET module via PWM; the module feeds power from a 24VDC charger into a 28W LED light. The sensor readings and light controls are all exposed on the MQTT broker.

**Basic hardware configuration**

![image](https://user-images.githubusercontent.com/85231028/188331018-2f5fdd82-5f84-42f6-afa9-ff0a245749f3.png)

**Hardware list**

*MCUs*

Raspberry Pi Pico 

Wemos D1 Mini (ESP8266) 

*Sensors*

DHT22 for humidity and temperature

Light dependent resistor (LDR) for brightness

*Drivers*

MOSFET module for modulating DC output

*Devices*

28W LED light
