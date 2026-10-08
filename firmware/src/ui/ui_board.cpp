#include "ui_board.h"
#include "ui_main.h"
#include "ui_journey.h"
#include "ui_track.h"
#include "theme.h"
#include "../board_config.h"
#include "../settings.h"
#include "../entur.h"
#include "../net_task.h"
#include "../ntp.h"
#include "../i18n.h"
#include <Arduino.h>

#define HDR_H   58
#define BAR_H   28
#define ROWS_Y  HDR_H
#define ROWS_H  (DISPLAY_HEIGHT - HDR_H - BAR_H)     // 394
#define ROW_N   8
#define ROW_H   (ROWS_H / ROW_N)                      // 49

struct Row {
    lv_obj_t *obj, *mark, *badge, *dest, *sub, *time, *status;
    int dep;            // index into entur_state().deps, -1 = empty
    char sj[72];
};
static Row       g_rows[ROW_N];
static lv_obj_t *g_hdr, *g_stop, *g_clock, *g_wx, *g_dots;
static lv_obj_t *g_rows_box, *g_empty, *g_empty_btn, *g_ticker, *g_menu;
static bool      g_blink;
static bool      g_force = true;   // next fill restyles every badge
static uint32_t  g_last_rotate;

static void set_text(lv_obj_t* l, const char* t) {
    if (strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t);
}
static void set_color(lv_obj_t* l, uint32_t c) {
    lv_color_t col = tc(c);
    if (lv_obj_get_style_text_color(l, 0).full != col.full) lv_obj_set_style_text_color(l, col, 0);
}

// ── interaction ──────────────────────────────────────────────────────────────
void ui_board_next_stop(int dir) {
    Settings& s = settings_get();
    if (s.board_mode != BOARD_SINGLE || s.stop_count < 2) return;
    s.cur_stop = (s.cur_stop + s.stop_count + dir) % s.stop_count;
    g_last_rotate = millis();
    ui_board_refresh();
}
static void hdr_cb(lv_event_t*) { ui_board_next_stop(1); }
static void gesture_cb(lv_event_t*) {
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (d == LV_DIR_LEFT)  ui_board_next_stop(1);
    if (d == LV_DIR_RIGHT) ui_board_next_stop(-1);
}
static void row_cb(lv_event_t* e) {
    Row* r = (Row*)lv_event_get_user_data(e);
    if (r->dep < 0) return;
    entur_lock();
    const DepState& st = entur_state();
    if (r->dep >= st.count || strcmp(st.deps[r->dep].sj, r->sj)) { entur_unlock(); return; }
    Departure d = st.deps[r->dep];
    char stop_id[28]; strlcpy(stop_id, d.stop < st.stop_n ? st.stop_ids[d.stop] : "", sizeof(stop_id));
    entur_unlock();
    ui_journey_open(d, stop_id);
}
static void menu_cb(lv_event_t*) { ui_main_goto_tab(PAGE_STOPS); }

// ── build ────────────────────────────────────────────────────────────────────
void ui_board_build(lv_obj_t* parent) {
    const Theme& t = theme();
    g_hdr = ui_plain(parent);
    lv_obj_set_size(g_hdr, DISPLAY_WIDTH, HDR_H);
    lv_obj_set_style_bg_color(g_hdr, tc(t.hdr_bg), 0);
    lv_obj_set_style_bg_opa(g_hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_hdr, tc(t.line), 0);
    lv_obj_set_style_border_width(g_hdr, 1, 0);
    lv_obj_set_style_border_side(g_hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_add_flag(g_hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_hdr, hdr_cb, LV_EVENT_CLICKED, nullptr);

    g_stop = ui_label(g_hdr, &font_b20, t.hdr_text, "");
    ui_one_line(g_stop, 190);
    lv_obj_set_pos(g_stop, 10, 6);
    g_clock = ui_label(g_hdr, &font_b20, t.hdr_text, "--:--");
    lv_obj_set_width(g_clock, 66);
    lv_obj_set_style_text_align(g_clock, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_clock, DISPLAY_WIDTH - 76, 6);
    g_wx = ui_label(g_hdr, &font_m14, t.hdr_text, "");
    lv_obj_set_style_text_opa(g_wx, LV_OPA_70, 0);
    ui_one_line(g_wx, 186);
    lv_obj_set_pos(g_wx, 10, 34);
    g_dots = ui_label(g_hdr, &font_m14, t.hdr_text, "");
    lv_obj_set_style_text_opa(g_dots, LV_OPA_70, 0);
    lv_obj_set_width(g_dots, 76);
    lv_obj_set_style_text_align(g_dots, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_dots, DISPLAY_WIDTH - 86, 34);

    g_rows_box = ui_plain(parent);
    lv_obj_set_size(g_rows_box, DISPLAY_WIDTH, ROWS_H);
    lv_obj_set_pos(g_rows_box, 0, ROWS_Y);
    lv_obj_add_flag(g_rows_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_rows_box, gesture_cb, LV_EVENT_GESTURE, nullptr);

    for (int i = 0; i < ROW_N; i++) {
        Row& r = g_rows[i];
        r.dep = -1; r.sj[0] = 0;
        r.obj = ui_plain(g_rows_box);
        lv_obj_set_size(r.obj, DISPLAY_WIDTH, ROW_H);
        lv_obj_set_pos(r.obj, 0, i * ROW_H);
        lv_obj_set_style_bg_color(r.obj, tc(i & 1 ? t.row_alt : t.bg), 0);
        lv_obj_set_style_bg_opa(r.obj, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(r.obj, tc(t.line), LV_STATE_PRESSED);
        lv_obj_add_flag(r.obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(r.obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(r.obj, row_cb, LV_EVENT_CLICKED, &r);
        r.mark = ui_plain(r.obj);                       // accent bar on the tracked line
        lv_obj_set_size(r.mark, 4, ROW_H);
        lv_obj_set_style_bg_color(r.mark, tc(t.accent), 0);
        lv_obj_set_style_bg_opa(r.mark, LV_OPA_COVER, 0);
        lv_obj_add_flag(r.mark, LV_OBJ_FLAG_HIDDEN);
        r.badge = ui_badge(r.obj);
        lv_obj_set_pos(r.badge, 8, (ROW_H - 30) / 2);
        r.dest = ui_label(r.obj, &font_b16, t.text, "");
        ui_one_line(r.dest, 128);
        lv_obj_set_pos(r.dest, 70, 6);
        r.sub = ui_label(r.obj, &font_m12, t.dim, "");
        ui_one_line(r.sub, 128);
        lv_obj_set_pos(r.sub, 70, 28);
        r.time = ui_label(r.obj, &font_b20, t.text, "");
        lv_obj_set_width(r.time, 74);
        lv_obj_set_style_text_align(r.time, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(r.time, DISPLAY_WIDTH - 82, 5);
        r.status = ui_label(r.obj, &font_m12, t.dim, "");
        lv_obj_set_width(r.status, 74);
        lv_obj_set_style_text_align(r.status, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(r.status, DISPLAY_WIDTH - 82, 29);
        lv_obj_add_flag(r.obj, LV_OBJ_FLAG_HIDDEN);
    }
    g_empty = ui_label(g_rows_box, &font_m16, t.dim, "");
    lv_label_set_long_mode(g_empty, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_empty, DISPLAY_WIDTH - 40);
    lv_obj_set_style_text_align(g_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_empty, LV_ALIGN_CENTER, 0, -30);
    g_empty_btn = ui_button(g_rows_box, TR(LV_SYMBOL_PLUS "  Legg til stopp", LV_SYMBOL_PLUS "  Add a stop"), menu_cb);
    lv_obj_align(g_empty_btn, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t* bar = ui_plain(parent);
    lv_obj_set_size(bar, DISPLAY_WIDTH, BAR_H);
    lv_obj_set_pos(bar, 0, DISPLAY_HEIGHT - BAR_H);
    lv_obj_set_style_bg_color(bar, tc(t.hdr_bg), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, tc(t.line), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    g_ticker = ui_label(bar, &font_m14, t.dim, "");
    lv_label_set_long_mode(g_ticker, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_speed(g_ticker, 30, 0);
    lv_obj_set_width(g_ticker, DISPLAY_WIDTH - 52);
    lv_obj_set_pos(g_ticker, 6, 5);
    g_menu = lv_btn_create(bar);
    lv_obj_set_size(g_menu, 44, BAR_H);
    lv_obj_set_pos(g_menu, DISPLAY_WIDTH - 44, 0);
    lv_obj_set_style_radius(g_menu, 0, 0);
    lv_obj_set_style_shadow_width(g_menu, 0, 0);
    lv_obj_set_style_bg_color(g_menu, tc(t.line), 0);
    lv_obj_set_style_bg_color(g_menu, tc(t.accent), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(g_menu, 12);
    lv_obj_t* ml = ui_label(g_menu, &lv_font_montserrat_16, t.hdr_text, LV_SYMBOL_BARS);
    lv_obj_center(ml);
    lv_obj_add_event_cb(g_menu, menu_cb, LV_EVENT_CLICKED, nullptr);
    g_last_rotate = millis();
    ui_board_refresh();
}

// ── content ──────────────────────────────────────────────────────────────────
static void update_header(time_t now) {
    const Settings& s = settings_get();
    char buf[64];
    bool merged = s.board_mode == BOARD_MERGED && s.stop_count > 1;
    if (s.stop_count == 0) set_text(g_stop, TR("Avganger", "Departures"));
    else if (merged) {                         // merged view: name every stop on the board
        char names[MAX_STOPS * 42] = "";
        for (int i = 0; i < s.stop_count; i++) { if (i) strlcat(names, " \xC2\xB7 ", sizeof(names)); strlcat(names, s.stops[i].name, sizeof(names)); }
        set_text(g_stop, names);
    }
    else set_text(g_stop, s.stops[s.cur_stop < s.stop_count ? s.cur_stop : 0].name);
    lv_label_long_mode_t lm = merged ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT;
    if (lv_label_get_long_mode(g_stop) != lm) lv_label_set_long_mode(g_stop, lm);
    if (ntp_is_synced()) {
        struct tm tm; localtime_r(&now, &tm);
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        set_text(g_clock, buf);
    }
    // page dots (single mode with several stops)
    if (s.board_mode == BOARD_SINGLE && s.stop_count > 1) {
        size_t o = 0;
        for (int i = 0; i < s.stop_count && o < sizeof(buf) - 4; i++)
            o += snprintf(buf + o, sizeof(buf) - o, "%s", i == s.cur_stop ? "\xE2\x80\xA2" : "\xC2\xB7");   // • ·
        buf[o] = 0;
        set_text(g_dots, buf);
        lv_obj_set_style_text_font(g_dots, &font_b20, 0);
        lv_obj_set_y(g_dots, 28);
    } else set_text(g_dots, "");
}

void ui_board_weather_changed() {
    const Settings& s = settings_get();
    const LocalInfo& li = net_local_info();
    char buf[64];
    ui_track_summary(buf, sizeof(buf));          // a tracked line takes over the header's second line
    if (buf[0]) { set_text(g_wx, buf); set_color(g_wx, theme().accent); lv_obj_set_style_text_opa(g_wx, LV_OPA_COVER, 0); return; }
    set_color(g_wx, theme().hdr_text); lv_obj_set_style_text_opa(g_wx, LV_OPA_70, 0);
    if (!s.show_weather || !li.valid) { set_text(g_wx, ""); return; }
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0  %s  \xC2\xB7  %.0f m/s", li.temp_c, geo_wmo_short(li.wmo_code), li.wind_mps);
    set_text(g_wx, buf);
}

static void platform_text(const Departure& d, char* out, size_t n) {
    if (!d.quay[0]) { out[0] = 0; return; }
    if (d.mode == TM_RAIL) snprintf(out, n, TR("Spor %s", "Track %s"), d.quay);
    else snprintf(out, n, TR("Plf. %s", "Stop %s"), d.quay);
}

static void fill_row(Row& r, const Departure& d, int idx, time_t now, bool merged, const DepState& st) {
    const Theme& t = theme();
    const Settings& s = settings_get();
    r.dep = idx;
    if (g_force || strcmp(r.sj, d.sj)) {          // restyle the badge only when the row shows a different trip
        strlcpy(r.sj, d.sj, sizeof(r.sj));
        ui_badge_set(r.badge, d.line, d.mode, d.has_color, d.color, d.text_color);
    }
    set_text(r.dest, d.dest);
    if (entur_track_matches(d, d.stop)) lv_obj_clear_flag(r.mark, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(r.mark, LV_OBJ_FLAG_HIDDEN);

    char sub[96] = "", part[48];
    auto add = [&](const char* p) { if (!p[0]) return; if (sub[0]) strlcat(sub, " \xC2\xB7 ", sizeof(sub)); strlcat(sub, p, sizeof(sub)); };
    if (merged && d.stop < st.stop_n) add(st.stop_names[d.stop]);
    if (s.show_platform) { platform_text(d, part, sizeof(part)); add(part); }
    if (d.situation >= 0 && d.situation < st.sit_count && !merged) add(st.situations[d.situation]);
    if (!sub[0]) add(entur_mode_name(d.mode));
    if (d.situation >= 0) { char w[100]; snprintf(w, sizeof(w), LV_SYMBOL_WARNING " %s", sub); strlcpy(sub, w, sizeof(sub)); }
    set_text(r.sub, sub);
    set_color(r.sub, d.situation >= 0 ? t.warn : t.dim);

    char tbuf[16]; entur_format_time(d, now, tbuf, sizeof(tbuf));
    set_text(r.time, tbuf);
    bool unreachable = d.stop < s.stop_count && s.stops[d.stop].walk_min && d.expected - now < s.stops[d.stop].walk_min * 60;
    lv_obj_set_style_text_decor(r.time, d.cancelled ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE, 0);
    set_color(r.time, d.cancelled || unreachable ? t.dim : t.text);

    char st_txt[24] = ""; uint32_t st_col = t.dim; lv_text_decor_t st_dec = LV_TEXT_DECOR_NONE;
    long delay_s = (long)(d.expected - d.aimed);
    if (d.cancelled) { strlcpy(st_txt, TR("INNSTILT", "CANCELLED"), sizeof(st_txt)); st_col = t.bad; }
    else if (entur_leave_now(d, now)) { if (g_blink) strlcpy(st_txt, TR("GÅ NÅ", "LEAVE NOW"), sizeof(st_txt)); st_col = t.accent; }
    else if (d.realtime && delay_s >= 120) {
        struct tm tm; time_t a = d.aimed; localtime_r(&a, &tm);
        snprintf(st_txt, sizeof(st_txt), "%02d:%02d", tm.tm_hour, tm.tm_min);
        st_dec = LV_TEXT_DECOR_STRIKETHROUGH;
    } else if (!d.realtime) strlcpy(st_txt, TR("rutetid", "scheduled"), sizeof(st_txt));
    set_text(r.status, st_txt);
    set_color(r.status, st_col);
    lv_obj_set_style_text_decor(r.status, st_dec, 0);
    lv_obj_clear_flag(r.obj, LV_OBJ_FLAG_HIDDEN);
}

static int g_shown = 0;
int ui_board_row_count() { return g_shown; }

static void update_rows(time_t now) {
    const Settings& s = settings_get();
    const Theme& t = theme();
    bool merged = s.board_mode == BOARD_MERGED;
    int shown = 0;
    entur_lock();
    const DepState& st = entur_state();
    bool fresh = st.have_data && st.stops_ver == settings_stops_version();
    if (fresh) {
        for (int i = 0; i < st.count && shown < ROW_N; i++) {
            const Departure& d = st.deps[i];
            if (!entur_visible(d, now, merged, s.cur_stop)) continue;
            fill_row(g_rows[shown++], d, i, now, merged, st);
        }
    }
    // ticker: disruptions, or fetch status
    static char tick[MAX_SITUATIONS * (SITUATION_LEN + 8)]; tick[0] = 0;
    uint32_t tick_col = t.dim;
    if (!st.ok && st.fetch_count + st.fail_count > 0 && st.error[0]) {
        snprintf(tick, sizeof(tick), TR("Ingen kontakt med Entur (%s)", "Cannot reach Entur (%s)"), st.error);
        tick_col = t.bad;
    } else if (s.ticker && st.sit_count) {
        for (int i = 0; i < st.sit_count; i++) {
            if (tick[0]) strlcat(tick, "     \xE2\x80\xA2     ", sizeof(tick));
            strlcat(tick, st.situations[i], sizeof(tick));
        }
        tick_col = t.warn;
    } else if (st.have_data) {
        struct tm tm; time_t f = st.fetched_epoch; localtime_r(&f, &tm);
        snprintf(tick, sizeof(tick), TR("Sanntid fra Entur \xC2\xB7 %02d:%02d:%02d", "Live from Entur \xC2\xB7 %02d:%02d:%02d"), tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
    int have = st.have_data;
    entur_unlock();
    set_text(g_ticker, tick);
    set_color(g_ticker, tick_col);

    for (int i = shown; i < ROW_N; i++) { g_rows[i].dep = -1; g_rows[i].sj[0] = 0; lv_obj_add_flag(g_rows[i].obj, LV_OBJ_FLAG_HIDDEN); }
    g_shown = shown;

    // empty states
    const char* msg = nullptr; bool btn = false;
    if (s.stop_count == 0) { msg = TR("Ingen stopp valgt ennå.\nSøk opp holdeplassen din.", "No stops yet.\nSearch for your stop."); btn = true; }
    else if (!net_wifi_connected() && !have) msg = TR("Venter på WiFi...", "Waiting for WiFi...");
    else if (!fresh) msg = TR("Henter avganger...", "Loading departures...");
    else if (shown == 0) msg = TR("Ingen avganger de neste timene.", "No departures in the next hours.");
    if (msg) { set_text(g_empty, msg); lv_obj_clear_flag(g_empty, LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_add_flag(g_empty, LV_OBJ_FLAG_HIDDEN);
    if (btn) lv_obj_clear_flag(g_empty_btn, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_empty_btn, LV_OBJ_FLAG_HIDDEN);
}

void ui_board_refresh() {
    time_t now = time(nullptr);
    g_force = true;
    update_header(now);
    update_rows(now);
    g_force = false;
    ui_board_weather_changed();
}

void ui_board_tick() {
    const Settings& s = settings_get();
    g_blink = !g_blink;
    if (s.board_mode == BOARD_SINGLE && s.rotate_s && s.stop_count > 1 && millis() - g_last_rotate >= s.rotate_s * 1000UL) {
        ui_board_next_stop(1);
        return;
    }
    time_t now = time(nullptr);
    update_header(now);
    update_rows(now);
    ui_board_weather_changed();
}
