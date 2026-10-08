#pragma once
#include <lvgl.h>
void ui_settings_build(lv_obj_t* parent);
void ui_settings_set_status(const char* msg);
void ui_settings_location_changed();
void ui_settings_debug(char c);       // serial: 'K' opens the keyboard on the SSID field
