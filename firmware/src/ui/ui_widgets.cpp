#include "theme.h"
#include "../settings.h"
#include "../entur.h"
#include "../board_config.h"
#include <Arduino.h>

const Theme THEMES[] = {
    // name           bg        row_alt   hdr_bg    hdr_text  text      dim       accent    warn      bad       line      input_bg  mono   dark
    {"Entur",        0x0E1240, 0x151A52, 0x181C56, 0xFFFFFF, 0xFFFFFF, 0xAEB7E2, 0x5AC39A, 0xFFCA28, 0xFF5959, 0x393D79, 0x0B0E33, false, true},
    {"Amber LED",    0x000000, 0x0B0700, 0x140C00, 0xFFB000, 0xFFB000, 0xA06C00, 0xFFD54A, 0xFF8A00, 0xFF4A1C, 0x3A2600, 0x0A0600, true,  true},
    {"Phosphor",     0x000000, 0x04120A, 0x050E08, 0x3DF25A, 0x3DF25A, 0x1E9A45, 0x8CFF7A, 0xFFE44D, 0xFF3B3B, 0x0E5A27, 0x04120A, true,  true},
    {"Dag / Day",    0xFFFFFF, 0xEEF0F8, 0x181C56, 0xFFFFFF, 0x181C56, 0x5A5F8A, 0x1A8E60, 0xB45F00, 0xD31B1B, 0xC9CCE0, 0xF4F5FA, false, false},
};
const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);
const Theme& theme() { uint8_t t = settings_get().theme; return THEMES[t < THEME_COUNT ? t : 0]; }

lv_obj_t* ui_plain(lv_obj_t* p) {
    lv_obj_t* o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
lv_obj_t* ui_label(lv_obj_t* p, const lv_font_t* f, uint32_t color, const char* text) {
    lv_obj_t* l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, tc(color), 0);
    lv_label_set_text(l, text);
    return l;
}
void ui_one_line(lv_obj_t* l, lv_coord_t w) {
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, w);
    lv_obj_set_height(l, lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)));
}
lv_obj_t* ui_button(lv_obj_t* p, const char* text, lv_event_cb_t cb, void* user) {
    const Theme& t = theme();
    lv_obj_t* b = lv_btn_create(p);
    lv_obj_set_height(b, 38);
    lv_obj_set_style_bg_color(b, tc(t.input_bg), 0);
    lv_obj_set_style_bg_color(b, tc(t.line), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, tc(t.line), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_t* l = ui_label(b, &font_m14, t.text, text);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}
lv_obj_t* ui_textarea(lv_obj_t* p, const char* ph, const char* val, bool pw) {
    const Theme& t = theme();
    lv_obj_t* ta = lv_textarea_create(p);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, ph);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_style_text_font(ta, &font_m14, 0);
    if (pw) lv_textarea_set_password_mode(ta, true);
    if (val && val[0]) lv_textarea_set_text(ta, val);
    lv_obj_set_style_bg_color(ta, tc(t.input_bg), 0);
    lv_obj_set_style_border_color(ta, tc(t.line), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_text_color(ta, tc(t.text), 0);
    lv_obj_set_style_text_color(ta, tc(t.dim), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(ta, tc(t.accent), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(ta, tc(t.text), LV_PART_CURSOR | LV_STATE_FOCUSED);
    return ta;
}
void ui_style_dropdown(lv_obj_t* dd) {
    const Theme& t = theme();
    lv_obj_set_style_bg_color(dd, tc(t.input_bg), 0);
    lv_obj_set_style_border_color(dd, tc(t.line), 0);
    lv_obj_set_style_text_color(dd, tc(t.text), 0);
    lv_obj_set_style_text_font(dd, &font_m14, 0);
    lv_obj_t* list = lv_dropdown_get_list(dd);
    lv_obj_set_style_bg_color(list, tc(t.input_bg), 0);
    lv_obj_set_style_border_color(list, tc(t.line), 0);
    lv_obj_set_style_text_color(list, tc(t.text), 0);
    lv_obj_set_style_text_font(list, &font_m14, 0);
    lv_obj_set_style_bg_color(list, tc(t.line), LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(list, tc(t.line), LV_PART_SELECTED | LV_STATE_PRESSED);
}
void ui_style_switch(lv_obj_t* sw) {
    const Theme& t = theme();
    lv_obj_set_style_bg_color(sw, tc(t.line), 0);
    lv_obj_set_style_bg_color(sw, tc(t.accent), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, tc(t.dark ? 0xFFFFFF : 0xFFFFFF), LV_PART_KNOB);
}

lv_obj_t* ui_badge(lv_obj_t* p) {
    lv_obj_t* b = ui_plain(p);
    lv_obj_set_size(b, 54, 30);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, 52);
    lv_obj_center(l);
    return b;
}
void ui_badge_set(lv_obj_t* b, const char* line, uint8_t mode, bool has_color, uint32_t color, uint32_t text_color) {
    const Theme& t = theme();
    const Settings& s = settings_get();
    lv_obj_t* l = lv_obj_get_child(b, 0);
    size_t n = strlen(line);
    lv_obj_set_style_text_font(l, n <= 3 ? &font_b20 : n <= 5 ? &font_b16 : &font_m12, 0);
    lv_label_set_text(l, line[0] ? line : "?");
    lv_obj_center(l);
    if (t.mono || !s.line_colours) {
        lv_obj_set_style_bg_color(b, tc(t.bg), 0);
        lv_obj_set_style_border_color(b, tc(t.text), 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_text_color(l, tc(t.text), 0);
        if (!t.mono) {   // themed but colours turned off: solid mode colour
            lv_obj_set_style_bg_color(b, tc(entur_mode_color(mode)), 0);
            lv_obj_set_style_border_width(b, 0, 0);
            lv_obj_set_style_text_color(l, tc(0xFFFFFF), 0);
        }
        return;
    }
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, tc(has_color ? color : entur_mode_color(mode)), 0);
    lv_obj_set_style_text_color(l, tc(has_color ? text_color : 0xFFFFFF), 0);
}

// ── shared keyboard ──────────────────────────────────────────────────────────
static const char* kb_map_lc[] = {
    "q","w","e","r","t","y","u","i","o","p","\n",
    "a","s","d","f","g","h","j","k","l","\n",
    "ABC","z","x","c","v","b","n","m",LV_SYMBOL_BACKSPACE,"\n",
    "1#","\xc3\xa6","\xc3\xb8","\xc3\xa5",","," ",LV_SYMBOL_OK,""
};
static const lv_btnmatrix_ctrl_t kb_ctrl_lc[] = {
    1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,
    LV_KEYBOARD_CTRL_BTN_FLAGS|2, 1,1,1,1,1,1,1, LV_KEYBOARD_CTRL_BTN_FLAGS|2,
    LV_KEYBOARD_CTRL_BTN_FLAGS|1, 1,1,1,1, 4, LV_KEYBOARD_CTRL_BTN_FLAGS|2
};
static const char* kb_map_uc[] = {
    "Q","W","E","R","T","Y","U","I","O","P","\n",
    "A","S","D","F","G","H","J","K","L","\n",
    "abc","Z","X","C","V","B","N","M",LV_SYMBOL_BACKSPACE,"\n",
    "1#","\xc3\x86","\xc3\x98","\xc3\x85",","," ",LV_SYMBOL_OK,""
};
static lv_obj_t* g_kb;
static lv_obj_t* g_kb_scroll;       // container shrunk while the keyboard is up
static lv_coord_t g_kb_scroll_h;

static void kb_cb(lv_event_t* e) {
    lv_event_code_t c = lv_event_get_code(e);
    // the keyboard already forwards READY to the text area; pages handle it there
    if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) ui_kb_hide();
}
void ui_kb_init() {
    const Theme& t = theme();
    g_kb = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(g_kb, DISPLAY_WIDTH, UI_KB_H);
    lv_obj_align(g_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_map(g_kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_lc, kb_ctrl_lc);
    lv_keyboard_set_map(g_kb, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_uc, kb_ctrl_lc);
    lv_obj_set_style_bg_color(g_kb, tc(t.hdr_bg), 0);
    lv_obj_set_style_bg_color(g_kb, tc(t.input_bg), LV_PART_ITEMS);
    lv_obj_set_style_text_color(g_kb, tc(t.text), LV_PART_ITEMS);
    lv_obj_set_style_text_font(g_kb, &font_m16, LV_PART_ITEMS);
    lv_obj_set_style_border_color(g_kb, tc(t.line), LV_PART_ITEMS);
    lv_obj_set_style_border_width(g_kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(g_kb, tc(t.line), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(g_kb, tc(t.line), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_kb, kb_cb, LV_EVENT_ALL, nullptr);
    lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
}
void ui_kb_attach(lv_obj_t* ta, bool numeric, lv_obj_t* scroll_parent) {
    if (g_kb_scroll && g_kb_scroll != scroll_parent) lv_obj_set_height(g_kb_scroll, g_kb_scroll_h);
    lv_keyboard_set_textarea(g_kb, ta);
    lv_keyboard_set_mode(g_kb, numeric ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_clear_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    if (scroll_parent && g_kb_scroll != scroll_parent) {
        g_kb_scroll = scroll_parent;
        g_kb_scroll_h = lv_obj_get_height(scroll_parent);
        lv_area_t a; lv_obj_get_coords(scroll_parent, &a);
        lv_coord_t visible = (DISPLAY_HEIGHT - UI_KB_H) - a.y1;
        if (visible < g_kb_scroll_h) lv_obj_set_height(scroll_parent, visible);
    }
    lv_obj_update_layout(lv_scr_act());
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_OFF);
}
void ui_kb_hide() {
    if (!g_kb) return;
    lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(g_kb, nullptr);
    if (g_kb_scroll) { lv_obj_set_height(g_kb_scroll, g_kb_scroll_h); g_kb_scroll = nullptr; }
}
bool ui_kb_visible() { return g_kb && !lv_obj_has_flag(g_kb, LV_OBJ_FLAG_HIDDEN); }
