# Real-Time-Temperature-Sensor
This is code written in FreeRTOS for the Adafruit feather 32u board for the course Software for Real-Time and Embedded Systems at the faculty of Computer Science, KULeuven.

The device receives beacons from a gateway that contain the gateway id and seconds until next transmission.
The device then wakes up at that time to send the temperature via LoRaWAN and goes back to sleep again.
The goal was to create a power efficient device that meets the Real-Time goals.

I received a 14/20 for this project.
