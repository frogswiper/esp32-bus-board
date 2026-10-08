#include "ui_main.h"
#include "theme.h"
#include "ui_board.h"
#include "ui_stops.h"
#include "ui_info.h"
#include "ui_settings.h"
#include "ui_journey.h"
#include "ui_track.h"
#include "../board_config.h"
#include "../i18n.h"
#include <initializer_list>

static lv_obj_t* g_tabview = nullptr;
static lv_obj_t* g_nav     = nullptr;
#define NAV_N 5
static lv_obj_t* g_nav_btns[NAV_N] = {};
static int       g_active  = 0;
static bool      g_fetch_req = false;

static const char* TAB_ICONS[] = {LV_SYMBOL_LIST, LV_SYMBOL_EYE_OPEN, LV_SYMBOL_HOME, LV_SYMBOL_CHARGE, LV_SYMBOL_SETTINGS};
static const int   TAB_PAGES[] = {PAGE_BOARD, PAGE_TRACK, PAGE_STOPS, PAGE_INFO, PAGE_SETTINGS};

static void update_nav() {
    const Theme& t = theme();
    for (int i = 0; i < NAV_N; i++) {
        lv_color_t col = tc(TAB_PAGES[i] == g_active ? t.accent : t.dim);
        for (uint32_t c = 0; c < lv_obj_get_child_cnt(g_nav_btns[i]); c++)
            lv_obj_set_style_text_color(lv_obj_get_child(g_nav_btns[i], c), col, 0);
    }
    if (g_active == PAGE_BOARD || g_active == PAGE_JOURNEY) lv_obj_add_flag(g_nav, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(g_nav, LV_OBJ_FLAG_HIDDEN);
}

static void nav_btn_cb(lv_event_t* e) { ui_main_goto_tab((int)(intptr_t)lv_event_get_user_data(e)); }

void ui_main_init() {
    const Theme& t = theme();
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, tc(t.bg), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    g_tabview = lv_tabview_create(scr, LV_DIR_TOP, 0);
    lv_obj_set_size(g_tabview, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_pos(g_tabview, 0, 0);
    lv_obj_set_style_bg_color(g_tabview, tc(t.bg), 0);
    lv_obj_set_style_border_width(g_tabview, 0, 0);
    lv_obj_set_style_pad_all(g_tabview, 0, 0);
    lv_obj_clear_flag(lv_tabview_get_content(g_tabview), LV_OBJ_FLAG_SCROLLABLE);  // no swipe between pages

    lv_obj_t* tabs[PAGE_COUNT];
    const char* names[PAGE_COUNT] = {"Board", "Stops", "Info", "Settings", "Journey", "Track"};
    for (int i = 0; i < PAGE_COUNT; i++) {
        tabs[i] = lv_tabview_add_tab(g_tabview, names[i]);
        lv_obj_set_style_pad_all(tabs[i], 0, 0);
        lv_obj_set_style_bg_color(tabs[i], tc(t.bg), 0);
        lv_obj_set_style_bg_opa(tabs[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(tabs[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    for (int i : {PAGE_STOPS, PAGE_INFO, PAGE_SETTINGS, PAGE_TRACK}) lv_obj_set_style_pad_bottom(tabs[i], TAB_BAR_HEIGHT, 0);

    ui_board_build(tabs[PAGE_BOARD]);
    ui_stops_build(tabs[PAGE_STOPS]);
    ui_info_build(tabs[PAGE_INFO]);
    ui_settings_build(tabs[PAGE_SETTINGS]);
    ui_journey_build(tabs[PAGE_JOURNEY]);
    ui_track_build(tabs[PAGE_TRACK]);

    // bottom navigation bar (hidden on the board and journey pages)
    const char* labels[NAV_N] = {TR("Tavle", "Board"), TR("Følg", "Track"), TR("Stopp", "Stops"), "Info", TR("Oppsett", "Settings")};
    g_nav = lv_obj_create(scr);
    lv_obj_set_size(g_nav, DISPLAY_WIDTH, TAB_BAR_HEIGHT);
    lv_obj_set_pos(g_nav, 0, CONTENT_HEIGHT);
    lv_obj_set_style_bg_color(g_nav, tc(t.hdr_bg), 0);
    lv_obj_set_style_border_color(g_nav, tc(t.line), 0);
    lv_obj_set_style_border_width(g_nav, 1, 0);
    lv_obj_set_style_border_side(g_nav, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(g_nav, 0, 0);
    lv_obj_set_style_pad_all(g_nav, 0, 0);
    lv_obj_set_flex_flow(g_nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_nav, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(g_nav, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < NAV_N; i++) {
        lv_obj_t* btn = lv_btn_create(g_nav);
        g_nav_btns[i] = btn;
        lv_obj_set_size(btn, DISPLAY_WIDTH / NAV_N, TAB_BAR_HEIGHT);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(btn, 2, 0);
        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, TAB_ICONS[i]);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_font(lbl, &font_m12, 0);
        lv_obj_add_event_cb(btn, nav_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)TAB_PAGES[i]);
    }
    ui_kb_init();
    g_active = PAGE_BOARD;
    update_nav();
}

void ui_main_goto_tab(int idx) {
    if (idx < 0 || idx >= PAGE_COUNT) return;
    ui_kb_hide();
    g_active = idx;
    lv_tabview_set_act(g_tabview, idx, LV_ANIM_OFF);
    update_nav();
    if (idx == PAGE_BOARD)   ui_board_refresh();
    if (idx == PAGE_STOPS)   ui_stops_refresh();
    if (idx == PAGE_INFO)    ui_info_refresh();
    if (idx == PAGE_JOURNEY) ui_journey_refresh();
    if (idx == PAGE_TRACK)   { ui_track_options_changed(); ui_track_request(); }
}
int  ui_main_active_tab() { return g_active; }
void ui_main_request_fetch() { g_fetch_req = true; }
bool ui_main_consume_fetch_request() { bool r = g_fetch_req; g_fetch_req = false; return r; }
