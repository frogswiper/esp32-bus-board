#pragma once
#include <lvgl.h>
#include "../entur.h"
void ui_journey_build(lv_obj_t* parent);
void ui_journey_open(const Departure& d, const char* stop_id);   // fetch + show the trip's calls
void ui_journey_refresh();       // redraw from entur_journey()
void ui_journey_tick();          // every few seconds while visible: re-fetch for live positions
