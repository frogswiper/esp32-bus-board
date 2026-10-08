#pragma once
#include <stdint.h>
#include <stdbool.h>
// Web panel on port 80 (+ mDNS esp32-busboard.local): live board JSON, stop editor, settings, Wi-Fi scan,
// screenshot, alert log, Prometheus metrics, OTA.
void        web_server_start();           // call once WiFi is up (safe to call again)
void        web_server_loop();            // call from loop()
void        web_server_arm_ota(uint32_t seconds);
bool        web_server_ota_armed();
const char* web_server_hostname();        // "esp32-busboard"
bool        web_server_started();
