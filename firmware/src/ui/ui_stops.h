#pragma once
#include <lvgl.h>
void ui_stops_build(lv_obj_t* parent);
void ui_stops_refresh();          // favourites list
void ui_stops_hits_changed();     // search / nearby results arrived
void ui_stops_debug(char c);      // serial: 'N' nearby, 'F' focus search
