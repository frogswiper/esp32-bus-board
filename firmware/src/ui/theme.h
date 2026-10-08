#pragma once
#include <lvgl.h>
#include <stdint.h>

// Colour themes. The flush callback inverts every pixel for this panel, so these are the real colours.
struct Theme {
    const char* name;
    uint32_t bg;          // page background
    uint32_t row_alt;     // alternate row stripe
    uint32_t hdr_bg;      // header / nav background
    uint32_t hdr_text;
    uint32_t text;        // primary text
    uint32_t dim;         // secondary text
    uint32_t accent;      // highlights, "leave now", active nav
    uint32_t warn;        // disruption ticker
    uint32_t bad;         // cancelled
    uint32_t line;        // dividers / borders
    uint32_t input_bg;    // text areas, buttons
    bool     mono;        // monochrome badges (outline in text colour) instead of line colours
    bool     dark;
};

extern const Theme THEMES[];
extern const int   THEME_COUNT;
const Theme& theme();
inline lv_color_t tc(uint32_t hex) { return lv_color_hex(hex); }

LV_FONT_DECLARE(font_m12);
LV_FONT_DECLARE(font_m14);
LV_FONT_DECLARE(font_m16);
LV_FONT_DECLARE(font_m20);
LV_FONT_DECLARE(font_b16);
LV_FONT_DECLARE(font_b20);
LV_FONT_DECLARE(font_b24);
LV_FONT_DECLARE(font_b36);

// shared widget helpers (ui_widgets.cpp)
lv_obj_t* ui_plain(lv_obj_t* parent);                                  // transparent, borderless, no padding/scroll
lv_obj_t* ui_label(lv_obj_t* parent, const lv_font_t* f, uint32_t color, const char* text = "");
void      ui_one_line(lv_obj_t* label, lv_coord_t width);                 // single line, "..." when too long
lv_obj_t* ui_button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* user = nullptr);
lv_obj_t* ui_textarea(lv_obj_t* parent, const char* placeholder, const char* value, bool password = false);
void      ui_style_dropdown(lv_obj_t* dd);
void      ui_style_switch(lv_obj_t* sw);
lv_obj_t* ui_badge(lv_obj_t* parent);                                  // line-number badge (label child 0)
void      ui_badge_set(lv_obj_t* badge, const char* line, uint8_t mode, bool has_color, uint32_t color, uint32_t text_color);

// shared on-screen keyboard (lives on the top layer, used by every page)
void      ui_kb_init();
void      ui_kb_attach(lv_obj_t* ta, bool numeric, lv_obj_t* scroll_parent);   // READY arrives on the text area
void      ui_kb_hide();
bool      ui_kb_visible();
#define   UI_KB_H 200
