#pragma once
#include <lvgl.h>
void ui_track_build(lv_obj_t* parent);
void ui_track_refresh();          // redraw from entur_tracker()
void ui_track_options_changed();  // departures changed: rebuild the stop / line pickers
void ui_track_summary(char* out, size_t n);   // one-line status for the board header ("" = not tracking)
void ui_track_follow(const char* stop_id, const char* line, const char* dest);   // set tracking + open the page
void ui_track_request();          // re-select trips from the departures and fetch them
