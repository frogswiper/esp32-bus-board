#pragma once
#include <lvgl.h>

// Pages: 0 = Board (full screen, no nav bar), 1 = Stops, 2 = Info, 3 = Settings, 4 = Journey (opened from the board), 5 = Track a line
#define PAGE_BOARD    0
#define PAGE_STOPS    1
#define PAGE_INFO     2
#define PAGE_SETTINGS 3
#define PAGE_JOURNEY  4
#define PAGE_TRACK    5
#define PAGE_COUNT    6
void ui_main_init();
void ui_main_goto_tab(int idx);
int  ui_main_active_tab();
void ui_main_request_fetch();          // ask main loop for an immediate Entur poll
bool ui_main_consume_fetch_request();
