| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | -------- |

# Wi-Fi Passthrough example

Send and receive Wi-Fi data frame over Uart.

The source code based on the following.

* <https://github.com/espressif/esp-idf/tree/master/examples/wifi/getting_started/station>
* <https://github.com/espressif/esp-idf/blob/master/components/esp_netif/loopback/esp_netif_loopback.c>

## How to use example

STM32-F302R8 <--UART--> ESP32 <--Wi-Fi--> Wi-Fi AP

STM32 Project: <https://github.com/ohmusso/NUCLEO-F302R8/tree/wifi>

### Configure the project

Open the project configuration menu (`idf.py menuconfig`).

In the `Example Configuration` menu:

* Set the Wi-Fi configuration.
    * Set `WiFi SSID`.
    * Set `WiFi Password`.

Optional: If you need, change the other options according to your requirements.

### Build and Flash

Build the project and flash it to the board, then run the monitor tool to view the serial output:

Run `idf.py -p PORT flash monitor` to build, flash and monitor the project.

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for all the steps to configure and use the ESP-IDF to build projects.

* [ESP-IDF Getting Started Guide on ESP32](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
* [ESP-IDF Getting Started Guide on ESP32-S2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/get-started/index.html)
* [ESP-IDF Getting Started Guide on ESP32-C3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/index.html)

### ex

build: idf.py build
write binary: idf.py -p COM4 flash
monitor: idf.py -p COM4 monitor

## Troubleshooting

For any technical queries, please open an [issue](https://github.com/espressif/esp-idf/issues) on GitHub. We will get back to you soon.
