#include "ui_journey.h"
#include "ui_main.h"
#include "theme.h"
#include "ui_track.h"
#include "../board_config.h"
#include "../net_task.h"
#include "../i18n.h"
#include <Arduino.h>

static lv_obj_t *g_badge, *g_dest, *g_info, *g_list, *g_status;
static Departure g_dep;
static char      g_stop_id[28];
static uint32_t  g_last_fetch;
static int       g_drawn_count = -1;
static uint32_t  g_drawn_ms;

static void back_cb(lv_event_t*) { ui_main_goto_tab(PAGE_BOARD); }
static void follow_cb(lv_event_t*) { ui_track_follow(g_stop_id, g_dep.line, g_dep.dest); }

static void request() {
    entur_lock(); entur_journey().loading = true; entur_unlock();
    net_send(NC_JOURNEY, g_dep.date, g_stop_id, (double)g_dep.aimed, 0, g_dep.sj);
    g_last_fetch = millis();
}

void ui_journey_open(const Departure& d, const char* stop_id) {
    g_dep = d;
    strlcpy(g_stop_id, stop_id, sizeof(g_stop_id));
    entur_lock(); entur_journey().valid = false; entur_journey().count = 0; strlcpy(entur_journey().sj, d.sj, sizeof(entur_journey().sj)); entur_unlock();
    ui_badge_set(g_badge, d.line, d.mode, d.has_color, d.color, d.text_color);
    lv_label_set_text(g_dest, d.dest);
    g_drawn_count = -1;
    request();
    ui_main_goto_tab(PAGE_JOURNEY);
}

void ui_journey_build(lv_obj_t* parent) {
    const Theme& t = theme();
    lv_obj_t* hdr = ui_plain(parent);
    lv_obj_set_size(hdr, DISPLAY_WIDTH, 64);
    lv_obj_set_style_bg_color(hdr, tc(t.hdr_bg), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(hdr, tc(t.line), 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_t* back = lv_btn_create(hdr);
    lv_obj_set_size(back, 40, 64);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_t* bl = ui_label(back, &lv_font_montserrat_20, t.hdr_text, LV_SYMBOL_LEFT);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);
    g_badge = ui_badge(hdr);
    lv_obj_set_pos(g_badge, 40, 8);
    g_dest = ui_label(hdr, &font_b16, t.hdr_text, "");
    ui_one_line(g_dest, DISPLAY_WIDTH - 148);
    lv_obj_set_pos(g_dest, 100, 6);
    lv_obj_t* fb = lv_btn_create(hdr);                     // "track this line" from this stop
    lv_obj_set_size(fb, 44, 44);
    lv_obj_set_pos(fb, DISPLAY_WIDTH - 48, 10);
    lv_obj_set_style_bg_color(fb, tc(t.accent), 0);
    lv_obj_set_style_radius(fb, 8, 0);
    lv_obj_set_style_shadow_width(fb, 0, 0);
    lv_obj_t* fl = ui_label(fb, &lv_font_montserrat_20, t.dark ? 0x000000 : 0xFFFFFF, LV_SYMBOL_EYE_OPEN);
    lv_obj_center(fl);
    lv_obj_add_event_cb(fb, follow_cb, LV_EVENT_CLICKED, nullptr);
    g_info = ui_label(hdr, &font_m12, t.hdr_text, "");
    lv_obj_set_style_text_opa(g_info, LV_OPA_70, 0);
    ui_one_line(g_info, DISPLAY_WIDTH - 148);
    lv_obj_set_pos(g_info, 100, 28);
    g_status = ui_label(hdr, &font_m12, t.hdr_text, "");
    lv_obj_set_style_text_opa(g_status, LV_OPA_70, 0);
    lv_obj_set_pos(g_status, 40, 44);

    g_list = lv_obj_create(parent);
    lv_obj_set_size(g_list, DISPLAY_WIDTH, DISPLAY_HEIGHT - 64);
    lv_obj_set_pos(g_list, 0, 64);
    lv_obj_set_style_bg_color(g_list, tc(t.bg), 0);
    lv_obj_set_style_border_width(g_list, 0, 0);
    lv_obj_set_style_radius(g_list, 0, 0);
    lv_obj_set_style_pad_all(g_list, 0, 0);
    lv_obj_set_style_pad_row(g_list, 0, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_ACTIVE);
}

static void hhmm(time_t t, char* out, size_t n) {
    struct tm tm; localtime_r(&t, &tm);
    snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

void ui_journey_refresh() {
    const Theme& t = theme();
    entur_lock();
    const Journey& j = entur_journey();
    if (!j.valid) {
        lv_obj_clean(g_list);
        lv_obj_t* l = ui_label(g_list, &font_m14, t.dim, j.loading ? TR("Henter rute...", "Loading trip...") : TR("Fant ikke turen.", "Trip not found."));
        lv_obj_set_style_pad_all(l, 16, 0);
        entur_unlock();
        g_drawn_count = -1;
        return;
    }
    if (g_drawn_count == j.count && g_drawn_ms == j.fetched_ms) { entur_unlock(); return; }
    bool first = g_drawn_count < 0;
    lv_coord_t keep_y = lv_obj_get_scroll_y(g_list);
    g_drawn_count = j.count; g_drawn_ms = j.fetched_ms;
    char info[64];
    int remaining = j.here >= 0 ? j.count - 1 - j.here : j.count;
    if (j.here >= 0) snprintf(info, sizeof(info), TR("%d stopp etter ditt \xC2\xB7 %d totalt", "%d stops after yours \xC2\xB7 %d total"), remaining, j.count);
    else snprintf(info, sizeof(info), TR("%s \xC2\xB7 %d stopp", "%s \xC2\xB7 %d stops"), entur_mode_name(j.mode), j.count);
    lv_label_set_text(g_info, info);
    if (j.dest[0]) lv_label_set_text(g_dest, j.dest);
    lv_label_set_text(g_status, j.vehicle >= 0 && j.vehicle < j.count - 1 ? TR("\xE2\x86\x92 posisjon fra sanntid", "\xE2\x86\x92 live position") : "");

    lv_obj_clean(g_list);
    lv_obj_t* here_row = nullptr;
    for (int i = 0; i < j.count; i++) {
        const JourneyCall& c = j.calls[i];
        bool passed = i <= j.vehicle;
        bool here = i == j.here;
        lv_obj_t* row = ui_plain(g_list);
        lv_obj_set_size(row, DISPLAY_WIDTH, 30);
        if (here) { lv_obj_set_style_bg_color(row, tc(t.row_alt), 0); lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0); here_row = row; }
        uint32_t col = passed ? t.dim : t.text;
        char tb[8]; hhmm(c.expected, tb, sizeof(tb));
        lv_obj_t* tl = ui_label(row, here ? &font_b16 : &font_m14, c.cancelled ? t.bad : col, tb);
        lv_obj_set_pos(tl, 10, here ? 6 : 7);
        if (c.cancelled) lv_obj_set_style_text_decor(tl, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        if (labs((long)(c.expected - c.aimed)) >= 120) {
            char ab[8]; hhmm(c.aimed, ab, sizeof(ab));
            lv_obj_t* al = ui_label(row, &font_m12, t.dim, ab);
            lv_obj_set_style_text_decor(al, LV_TEXT_DECOR_STRIKETHROUGH, 0);
            lv_obj_set_pos(al, 58, 9);
        }
        // route line with stop dot; vehicle marker after the last passed stop
        lv_obj_t* ln = ui_plain(row);
        lv_obj_set_size(ln, 4, 30);
        lv_obj_set_pos(ln, 104, 0);
        lv_obj_set_style_bg_color(ln, tc(passed ? t.line : (j.has_color && !t.mono ? j.color : t.accent)), 0);
        lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
        lv_obj_t* dot = ui_plain(row);
        int ds = here ? 14 : 10;
        lv_obj_set_size(dot, ds, ds);
        lv_obj_set_pos(dot, 106 - ds / 2, 15 - ds / 2);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, tc(here ? t.accent : t.bg), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(dot, tc(passed ? t.line : t.text), 0);
        lv_obj_set_style_border_width(dot, 2, 0);
        if (i == j.vehicle && i < j.count - 1) {
            lv_obj_t* v = ui_label(row, &lv_font_montserrat_14, t.accent, LV_SYMBOL_DOWN);
            lv_obj_set_pos(v, 99, 17);
        }
        char nm[56];
        if (c.quay[0] && here) snprintf(nm, sizeof(nm), "%s (%s)", c.name, c.quay); else strlcpy(nm, c.name, sizeof(nm));
        lv_obj_t* nl = ui_label(row, here ? &font_b16 : &font_m14, col, nm);
        ui_one_line(nl, DISPLAY_WIDTH - 126);
        lv_obj_set_pos(nl, 120, here ? 6 : 7);
    }
    entur_unlock();
    lv_obj_update_layout(g_list);
    if (first && here_row) lv_obj_scroll_to_y(g_list, max(0, (int)lv_obj_get_y(here_row) - 120), LV_ANIM_OFF);
    else if (!first) lv_obj_scroll_to_y(g_list, keep_y, LV_ANIM_OFF);   // live refresh keeps the user's scroll position
}

void ui_journey_tick() {
    if (millis() - g_last_fetch > 30000 && !net_pending(NC_JOURNEY)) request();
}
