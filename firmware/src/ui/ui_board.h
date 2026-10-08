#pragma once
#include <lvgl.h>
void ui_board_build(lv_obj_t* parent);
void ui_board_refresh();          // rebuild the rows from the latest departures
void ui_board_tick();             // once per second: clock, countdowns, blinking
void ui_board_weather_changed();
void ui_board_next_stop(int dir); // single mode: cycle stops
int  ui_board_row_count();
