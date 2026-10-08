#include "ui_stops.h"
#include "ui_main.h"
#include "ui_board.h"
#include "theme.h"
#include "../board_config.h"
#include "../settings.h"
#include "../entur.h"
#include "../net_task.h"
#include "../i18n.h"
#include <Arduino.h>

static lv_obj_t *g_page, *g_fav_hdr, *g_favs, *g_ta, *g_status, *g_hits;
static lv_obj_t *g_modal, *g_m_title, *g_m_walk, *g_m_lines, *g_m_quays;
static int       g_edit = -1;

static void status(const char* m) { lv_label_set_text(g_status, m); }

static lv_obj_t* section(lv_obj_t* p, const char* text) {
    lv_obj_t* l = ui_label(p, &font_b16, theme().text, text);
    lv_obj_set_style_pad_top(l, 8, 0);
    return l;
}
static lv_obj_t* icon_btn(lv_obj_t* p, const char* sym, lv_event_cb_t cb, intptr_t user) {
    const Theme& t = theme();
    lv_obj_t* b = lv_btn_create(p);
    lv_obj_set_size(b, 36, 34);
    lv_obj_set_style_bg_color(b, tc(t.input_bg), 0);
    lv_obj_set_style_bg_color(b, tc(t.line), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, tc(t.line), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_t* l = ui_label(b, &lv_font_montserrat_16, t.text, sym);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)user);
    return b;
}

// ── favourites ───────────────────────────────────────────────────────────────
static void fav_open_cb(lv_event_t* e) {
    Settings& s = settings_get();
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < s.stop_count) { s.cur_stop = i; settings_save(); }
    ui_main_goto_tab(PAGE_BOARD);
}
static void fav_up_cb(lv_event_t* e)  { settings_move_stop_up((int)(intptr_t)lv_event_get_user_data(e)); ui_stops_refresh(); ui_main_request_fetch(); }
static void fav_del_cb(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    char m[80]; snprintf(m, sizeof(m), TR("Fjernet %s", "Removed %s"), settings_get().stops[i].name);
    settings_remove_stop(i);
    ui_stops_refresh(); status(m); ui_main_request_fetch();
}
static const uint8_t WALK_STEPS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 15, 20};
static const int WALK_N = sizeof(WALK_STEPS);
static void fav_edit_cb(lv_event_t* e) {
    const Settings& s = settings_get();
    g_edit = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_edit >= s.stop_count) return;
    const StopCfg& sc = s.stops[g_edit];
    lv_label_set_text(g_m_title, sc.name);
    int wi = 0; for (int k = 0; k < WALK_N; k++) if (WALK_STEPS[k] <= sc.walk_min) wi = k;
    lv_dropdown_set_selected(g_m_walk, wi);
    lv_textarea_set_text(g_m_lines, sc.lines);
    lv_textarea_set_text(g_m_quays, sc.quays);
    lv_obj_clear_flag(g_modal, LV_OBJ_FLAG_HIDDEN);
}
static void modal_close() { ui_kb_hide(); lv_obj_add_flag(g_modal, LV_OBJ_FLAG_HIDDEN); g_edit = -1; }
static void modal_save_cb(lv_event_t*) {
    Settings& s = settings_get();
    if (g_edit >= 0 && g_edit < s.stop_count) {
        StopCfg& sc = s.stops[g_edit];
        sc.walk_min = WALK_STEPS[lv_dropdown_get_selected(g_m_walk)];
        strlcpy(sc.lines, lv_textarea_get_text(g_m_lines), sizeof(sc.lines));
        strlcpy(sc.quays, lv_textarea_get_text(g_m_quays), sizeof(sc.quays));
        settings_save();
    }
    modal_close(); ui_stops_refresh(); ui_board_refresh();
}
static void modal_cancel_cb(lv_event_t*) { modal_close(); }
static void modal_ta_cb(lv_event_t* e) { ui_kb_attach((lv_obj_t*)lv_event_get_target(e), false, nullptr); }

void ui_stops_refresh() {
    const Theme& t = theme();
    const Settings& s = settings_get();
    char buf[96];
    snprintf(buf, sizeof(buf), TR("Mine stopp  (%d/%d)", "My stops  (%d/%d)"), s.stop_count, MAX_STOPS);
    lv_label_set_text(g_fav_hdr, buf);
    lv_obj_clean(g_favs);
    if (s.stop_count == 0) {
        lv_obj_t* l = ui_label(g_favs, &font_m14, t.dim, TR("Ingen ennå - søk under eller trykk \"Nær meg\".", "None yet - search below or tap \"Near me\"."));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, LV_PCT(100));
    }
    for (int i = 0; i < s.stop_count; i++) {
        const StopCfg& sc = s.stops[i];
        lv_obj_t* row = ui_plain(g_favs);
        lv_obj_set_size(row, LV_PCT(100), 48);
        lv_obj_set_style_bg_color(row, tc(t.input_bg), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, tc(t.line), LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, fav_open_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        int btns = i > 0 ? 3 : 2;
        lv_obj_t* nm = ui_label(row, &font_b16, t.text, sc.name);
        ui_one_line(nm, 248 - btns * 40 - 8);
        lv_obj_set_pos(nm, 8, 5);
        char sub[96];
        snprintf(sub, sizeof(sub), "%s%s%s \xC2\xB7 %d min %s", sc.lines[0] ? TR("Linjer ", "Lines ") : TR("Alle linjer", "All lines"), sc.lines,
                 sc.quays[0] ? (String(" \xC2\xB7 ") + TR("plf. ", "stop ") + sc.quays).c_str() : "", sc.walk_min, TR("gange", "walk"));
        lv_obj_t* sl = ui_label(row, &font_m12, t.dim, sub);
        ui_one_line(sl, 248 - btns * 40 - 8);
        lv_obj_set_pos(sl, 8, 27);
        int x = 248 - 40;
        lv_obj_set_pos(icon_btn(row, LV_SYMBOL_TRASH, fav_del_cb, i), x, 7); x -= 40;
        lv_obj_set_pos(icon_btn(row, LV_SYMBOL_EDIT, fav_edit_cb, i), x, 7); x -= 40;
        if (i > 0) lv_obj_set_pos(icon_btn(row, LV_SYMBOL_UP, fav_up_cb, i), x, 7);
    }
}

// ── search ───────────────────────────────────────────────────────────────────
static void do_search() {
    const char* q = lv_textarea_get_text(g_ta);
    if (strlen(q) < 2) { status(TR("Skriv minst to bokstaver", "Type at least two letters")); return; }
    if (!net_wifi_connected()) { status(TR("Ingen WiFi", "No WiFi")); return; }
    ui_kb_hide();
    status(TR("Søker...", "Searching..."));
    net_send(NC_STOP_SEARCH, nullptr, nullptr, 0, 0, q);
}
static void search_cb(lv_event_t*) { do_search(); }
static void ta_cb(lv_event_t* e) {
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_FOCUSED || c == LV_EVENT_CLICKED) ui_kb_attach(g_ta, false, g_page);
    else if (c == LV_EVENT_READY) do_search();
}
static void nearby_cb(lv_event_t*) {
    if (!net_wifi_connected()) { status(TR("Ingen WiFi", "No WiFi")); return; }
    status(TR("Finner stopp i nærheten...", "Finding stops nearby..."));
    net_send(NC_STOP_NEARBY);
}
static void hit_cb(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    entur_lock();
    StopHits& h = entur_hits();
    if (i >= h.count) { entur_unlock(); return; }
    StopHit hit = h.hits[i];
    entur_unlock();
    char m[96];
    if (settings_get().stop_count >= MAX_STOPS) snprintf(m, sizeof(m), TR("Maks %d stopp - fjern ett først", "Max %d stops - remove one first"), MAX_STOPS);
    else if (settings_add_stop(hit.id, hit.name, hit.lat, hit.lon)) {
        snprintf(m, sizeof(m), TR("La til %s", "Added %s"), hit.name);
        settings_get().cur_stop = settings_get().stop_count - 1;
        settings_save();
        ui_main_request_fetch();
    } else snprintf(m, sizeof(m), TR("%s er allerede lagt til", "%s is already added"), hit.name);
    status(m);
    ui_stops_refresh();
}

void ui_stops_hits_changed() {
    const Theme& t = theme();
    lv_obj_clean(g_hits);
    entur_lock();
    const StopHits& h = entur_hits();
    char m[64];
    if (!h.ok) strlcpy(m, TR("Søket feilet - prøv igjen", "Search failed - try again"), sizeof(m));
    else if (!h.count) strlcpy(m, TR("Ingen treff", "No matches"), sizeof(m));
    else snprintf(m, sizeof(m), TR("%d treff - trykk for å legge til", "%d matches - tap to add"), h.count);
    for (int i = 0; i < h.count; i++) {
        const StopHit& s = h.hits[i];
        lv_obj_t* row = ui_plain(g_hits);
        lv_obj_set_size(row, LV_PCT(100), 44);
        lv_obj_set_style_border_color(row, tc(t.line), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_bg_color(row, tc(t.line), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, hit_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* nm = ui_label(row, &font_b16, t.text, s.name);
        ui_one_line(nm, 210);
        lv_obj_set_pos(nm, 2, 4);
        char sub[96] = "";
        strlcpy(sub, s.locality, sizeof(sub));
        for (int k = 0; k < TM_OTHER; k++) if (s.modes & (1 << k)) { strlcat(sub, " \xC2\xB7 ", sizeof(sub)); strlcat(sub, entur_mode_name(k), sizeof(sub)); }
        if (s.dist_km >= 0) { char d[16]; if (s.dist_km < 1) snprintf(d, sizeof(d), " \xC2\xB7 %d m", (int)(s.dist_km * 1000)); else snprintf(d, sizeof(d), " \xC2\xB7 %.1f km", s.dist_km); strlcat(sub, d, sizeof(sub)); }
        lv_obj_t* sl = ui_label(row, &font_m12, t.dim, sub);
        ui_one_line(sl, 210);
        lv_obj_set_pos(sl, 2, 25);
        lv_obj_t* plus = ui_label(row, &lv_font_montserrat_20, t.accent, LV_SYMBOL_PLUS);
        lv_obj_align(plus, LV_ALIGN_RIGHT_MID, -4, 0);
    }
    entur_unlock();
    status(m);
}

// ── build ────────────────────────────────────────────────────────────────────
void ui_stops_build(lv_obj_t* parent) {
    const Theme& t = theme();
    g_page = lv_obj_create(parent);
    lv_obj_set_size(g_page, DISPLAY_WIDTH, CONTENT_HEIGHT);
    lv_obj_set_style_bg_color(g_page, tc(t.bg), 0);
    lv_obj_set_style_border_width(g_page, 0, 0);
    lv_obj_set_style_radius(g_page, 0, 0);
    lv_obj_set_style_pad_all(g_page, 12, 0);
    lv_obj_set_style_pad_row(g_page, 6, 0);
    lv_obj_set_flex_flow(g_page, LV_FLEX_FLOW_COLUMN);

    g_fav_hdr = section(g_page, "");
    lv_obj_set_style_pad_top(g_fav_hdr, 0, 0);
    g_favs = ui_plain(g_page);
    lv_obj_set_size(g_favs, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_favs, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_favs, 6, 0);

    section(g_page, TR("Legg til stopp", "Add a stop"));
    lv_obj_t* srow = ui_plain(g_page);
    lv_obj_set_size(srow, LV_PCT(100), 40);
    lv_obj_set_flex_flow(srow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(srow, 6, 0);
    g_ta = ui_textarea(srow, TR("Holdeplass, f.eks. Jernbanetorget", "Stop, e.g. Jernbanetorget"), "");
    lv_obj_set_flex_grow(g_ta, 1);
    lv_obj_add_event_cb(g_ta, ta_cb, LV_EVENT_ALL, nullptr);
    lv_obj_t* sb = ui_button(srow, LV_SYMBOL_RIGHT, search_cb);
    lv_obj_set_width(sb, 44);
    lv_obj_t* nb = ui_button(g_page, TR(LV_SYMBOL_GPS "  Nær meg", LV_SYMBOL_GPS "  Near me"), nearby_cb);
    lv_obj_set_width(nb, LV_PCT(100));
    g_status = ui_label(g_page, &font_m12, t.dim, "");
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_status, LV_PCT(100));
    g_hits = ui_plain(g_page);
    lv_obj_set_size(g_hits, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_hits, LV_FLEX_FLOW_COLUMN);

    // per-stop editor (modal on the top layer, above the page but below the keyboard)
    g_modal = ui_plain(lv_layer_top());
    lv_obj_set_size(g_modal, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_style_bg_color(g_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_modal, LV_OPA_60, 0);
    lv_obj_add_flag(g_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* card = lv_obj_create(g_modal);
    lv_obj_set_size(card, DISPLAY_WIDTH - 16, 272);
    lv_obj_set_pos(card, 8, 6);
    lv_obj_set_style_bg_color(card, tc(t.bg), 0);
    lv_obj_set_style_border_color(card, tc(t.line), 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    g_m_title = ui_label(card, &font_b16, t.text, "");
    ui_one_line(g_m_title, LV_PCT(100));
    lv_obj_t* wr = ui_plain(card);
    lv_obj_set_size(wr, LV_PCT(100), 40);
    lv_obj_t* wl = ui_label(wr, &font_m14, t.text, TR("Gangtid", "Walk time"));
    lv_obj_align(wl, LV_ALIGN_LEFT_MID, 0, 0);
    g_m_walk = lv_dropdown_create(wr);
    lv_dropdown_set_options(g_m_walk, "0 min\n1 min\n2 min\n3 min\n4 min\n5 min\n6 min\n7 min\n8 min\n10 min\n12 min\n15 min\n20 min");
    lv_obj_set_width(g_m_walk, 110);
    lv_obj_align(g_m_walk, LV_ALIGN_RIGHT_MID, 0, 0);
    ui_style_dropdown(g_m_walk);
    ui_label(card, &font_m12, t.dim, TR("Bare disse linjene (komma, -31 skjuler 31)", "Only these lines (comma, -31 hides 31)"));
    g_m_lines = ui_textarea(card, TR("alle", "all"), "");
    lv_obj_add_event_cb(g_m_lines, modal_ta_cb, LV_EVENT_FOCUSED, nullptr);
    ui_label(card, &font_m12, t.dim, TR("Bare disse plattformene / sporene", "Only these platforms / tracks"));
    g_m_quays = ui_textarea(card, TR("alle", "all"), "");
    lv_obj_add_event_cb(g_m_quays, modal_ta_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_t* br = ui_plain(card);
    lv_obj_set_size(br, LV_PCT(100), 42);
    lv_obj_set_style_pad_top(br, 4, 0);
    lv_obj_t* cb = ui_button(br, TR("Avbryt", "Cancel"), modal_cancel_cb);
    lv_obj_set_width(cb, 100); lv_obj_align(cb, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* ok = ui_button(br, TR("Lagre", "Save"), modal_save_cb);
    lv_obj_set_width(ok, 100); lv_obj_align(ok, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(ok, tc(t.accent), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ok, 0), tc(t.dark ? 0x000000 : 0xFFFFFF), 0);
    lv_obj_add_flag(g_modal, LV_OBJ_FLAG_HIDDEN);

    ui_stops_refresh();
}

void ui_stops_debug(char c) {
    if (c == 'N') nearby_cb(nullptr);
    if (c == 'F') { lv_textarea_set_text(g_ta, "Hønefoss"); do_search(); }
    if (c == 'E' && settings_get().stop_count) { lv_event_t e = {}; e.user_data = (void*)0; fav_edit_cb(&e); }
    if (c == 'Q') modal_close();
}
