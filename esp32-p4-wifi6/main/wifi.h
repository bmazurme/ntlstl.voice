#pragma once

// Connects to the WiFi network configured via `idf.py menuconfig`
// (P4 Voice Client Configuration -> WiFi SSID/Password) and blocks until an
// IP address has been obtained. On the ESP32-P4 the radio itself lives on
// the on-board ESP32-C6; esp_wifi_remote makes this call route to it.
void wifi_connect_blocking(void);
