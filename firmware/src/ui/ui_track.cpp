#include "ui_track.h"
#include "ui_main.h"
#include "theme.h"
#include "../board_config.h"
#include "../settings.h"
#include "../entur.h"
#include "../net_task.h"
#include "../i18n.h"
#include <Arduino.h>

#define PICK_H   48
#define CARD_H   78
#define STRIP_Y  (PICK_H + CARD_H + 8)
#define FOOT_H   26
#define STRIP_H  (CONTENT_HEIGHT - STRIP_Y - FOOT_H)

static lv_obj_t *g_dd_stop, *g_dd_line, *g_badge, *g_big, *g_small, *g_strip, *g_foot, *g_card;
// line picker entries for the selected stop: "line|dest" pairs, index 0 = not tracking
#define MAX_OPTS 16
static char g_opt_line[MAX_OPTS][10];
static char g_opt_dest[MAX_OPTS][44];
static int  g_opt_n;

static void hhmm(time_t t, char* out, size_t n) { struct tm tm; localtime_r(&t, &tm); snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min); }

static int stop_index_of(const char* id) {
    const Settings& s = settings_get();
    for (int i = 0; i < s.stop_count; i++) if (!strcmp(s.stops[i].id, id)) return i;
    return -1;
}

static void request_track() {
    entur_lock(); int n = entur_track_select(time(nullptr)); entur_unlock();
    if (n && !net_pending(NC_TRACK)) net_send(NC_TRACK);
    ui_track_refresh();
}

// ── pickers ──────────────────────────────────────────────────────────────────
static void fill_lines() {
    const Settings& s = settings_get();
    int si = lv_dropdown_get_selected(g_dd_stop);
    g_opt_n = 1; g_opt_line[0][0] = 0; g_opt_dest[0][0] = 0;
    static char opts[MAX_OPTS * 60];
    strlcpy(opts, TR("Velg linje...", "Choose line..."), sizeof(opts));
    int sel = 0;
    entur_lock();
    const DepState& st = entur_state();
    for (int i = 0; i < st.count && g_opt_n < MAX_OPTS; i++) {
        const Departure& d = st.deps[i];
        if (d.stop >= st.stop_n || si >= s.stop_count || strcmp(st.stop_ids[d.stop], s.stops[si].id)) continue;
        bool dup = false;
        for (int k = 1; k < g_opt_n; k++) if (!strcmp(g_opt_line[k], d.line) && !strcmp(g_opt_dest[k], d.dest)) dup = true;
        if (dup) continue;
        strlcpy(g_opt_line[g_opt_n], d.line, sizeof(g_opt_line[0]));
        strlcpy(g_opt_dest[g_opt_n], d.dest, sizeof(g_opt_dest[0]));
        char o[64]; snprintf(o, sizeof(o), "\n%s \xE2\x86\x92 %s", d.line, d.dest);
        strlcat(opts, o, sizeof(opts));
        if (!strcmp(s.track_stop, s.stops[si].id) && !strcasecmp(s.track_line, d.line) && !strcmp(s.track_dest, d.dest)) sel = g_opt_n;
        g_opt_n++;
    }
    entur_unlock();
    // keep the saved choice selectable even when it has no departures right now
    if (!sel && s.track_line[0] && si < s.stop_count && !strcmp(s.track_stop, s.stops[si].id) && g_opt_n < MAX_OPTS) {
        strlcpy(g_opt_line[g_opt_n], s.track_line, sizeof(g_opt_line[0]));
        strlcpy(g_opt_dest[g_opt_n], s.track_dest, sizeof(g_opt_dest[0]));
        char o[64]; snprintf(o, sizeof(o), "\n%s \xE2\x86\x92 %s", s.track_line, s.track_dest[0] ? s.track_dest : TR("alle", "all"));
        strlcat(opts, o, sizeof(opts));
        sel = g_opt_n++;
    }
    lv_dropdown_set_options(g_dd_line, opts);
    lv_dropdown_set_selected(g_dd_line, sel);
}
void ui_track_options_changed() {
    const Settings& s = settings_get();
    static char opts[MAX_STOPS * 42];
    opts[0] = 0;
    for (int i = 0; i < s.stop_count; i++) { if (i) strlcat(opts, "\n", sizeof(opts)); strlcat(opts, s.stops[i].name, sizeof(opts)); }
    if (!s.stop_count) strlcpy(opts, "-", sizeof(opts));
    int cur = lv_dropdown_get_selected(g_dd_stop);
    if (lv_dropdown_is_open(g_dd_stop)) return;          // don't yank the list from under the user's finger
    if (strcmp(lv_dropdown_get_options(g_dd_stop), opts)) {
        lv_dropdown_set_options(g_dd_stop, opts);
        int ti = stop_index_of(s.track_stop);
        cur = ti >= 0 ? ti : 0;
    }
    lv_dropdown_set_selected(g_dd_stop, cur);
    if (!lv_dropdown_is_open(g_dd_line) && !lv_dropdown_is_open(g_dd_stop)) fill_lines();
}
static void stop_cb(lv_event_t*) { fill_lines(); }
static void line_cb(lv_event_t*) {
    Settings& s = settings_get();
    int si = lv_dropdown_get_selected(g_dd_stop), li = lv_dropdown_get_selected(g_dd_line);
    if (li <= 0 || li >= g_opt_n || si >= s.stop_count) { s.track_line[0] = 0; s.track_dest[0] = 0; }
    else {
        strlcpy(s.track_stop, s.stops[si].id, sizeof(s.track_stop));
        strlcpy(s.track_line, g_opt_line[li], sizeof(s.track_line));
        strlcpy(s.track_dest, g_opt_dest[li], sizeof(s.track_dest));
    }
    settings_save();
    entur_lock(); entur_tracker().count = 0; entur_unlock();
    request_track();
}

// ── build ────────────────────────────────────────────────────────────────────
void ui_track_build(lv_obj_t* parent) {
    const Theme& t = theme();
    g_dd_stop = lv_dropdown_create(parent);
    lv_obj_set_size(g_dd_stop, 118, 38);
    lv_obj_set_pos(g_dd_stop, 8, 6);
    ui_style_dropdown(g_dd_stop);
    lv_obj_add_event_cb(g_dd_stop, stop_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    g_dd_line = lv_dropdown_create(parent);
    lv_obj_set_size(g_dd_line, DISPLAY_WIDTH - 140, 38);
    lv_obj_set_pos(g_dd_line, 132, 6);
    ui_style_dropdown(g_dd_line);
    lv_obj_add_event_cb(g_dd_line, line_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    g_card = ui_plain(parent);
    lv_obj_set_size(g_card, DISPLAY_WIDTH - 16, CARD_H);
    lv_obj_set_pos(g_card, 8, PICK_H + 2);
    lv_obj_set_style_bg_color(g_card, tc(t.hdr_bg), 0);
    lv_obj_set_style_bg_opa(g_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_card, 8, 0);
    lv_obj_set_style_border_color(g_card, tc(t.accent), 0);
    lv_obj_set_style_border_width(g_card, 2, 0);
    g_badge = ui_badge(g_card);
    lv_obj_set_pos(g_badge, 8, 10);
    g_big = ui_label(g_card, &font_b20, t.hdr_text, "");
    ui_one_line(g_big, DISPLAY_WIDTH - 16 - 76);
    lv_obj_set_pos(g_big, 70, 8);
    g_small = ui_label(g_card, &font_m12, t.hdr_text, "");
    lv_obj_set_style_text_opa(g_small, LV_OPA_80, 0);
    lv_label_set_long_mode(g_small, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_small, DISPLAY_WIDTH - 16 - 16);
    lv_obj_set_pos(g_small, 8, 44);

    g_strip = lv_obj_create(parent);
    lv_obj_set_size(g_strip, DISPLAY_WIDTH, STRIP_H);
    lv_obj_set_pos(g_strip, 0, STRIP_Y);
    lv_obj_set_style_bg_opa(g_strip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_strip, 0, 0);
    lv_obj_set_style_radius(g_strip, 0, 0);
    lv_obj_set_style_pad_all(g_strip, 0, 0);
    lv_obj_set_style_pad_row(g_strip, 0, 0);
    lv_obj_set_flex_flow(g_strip, LV_FLEX_FLOW_COLUMN);

    g_foot = ui_label(parent, &font_m14, t.dim, "");
    ui_one_line(g_foot, DISPLAY_WIDTH - 16);
    lv_obj_set_pos(g_foot, 8, CONTENT_HEIGHT - FOOT_H + 4);
    ui_track_options_changed();
}

// ── status text ──────────────────────────────────────────────────────────────
static void trip_status(const Journey& j, time_t now, char* big, size_t nb, char* small, size_t ns) {
    const JourneyCall& here = j.calls[j.here];
    int away = entur_track_stops_away(j);
    long sec = (long)(here.expected - now);
    int mins = sec > 0 ? (int)(sec / 60) : 0;
    char at[8]; hhmm(here.expected, at, sizeof(at));
    const char* est = j.estimated ? TR(" (beregnet)", " (estimated)") : "";
    if (j.vehicle < 0) {
        char dep[8]; hhmm(j.calls[0].expected, dep, sizeof(dep));
        snprintf(big, nb, TR("Ikke startet", "Not started"));
        snprintf(small, ns, TR("Går fra %s kl. %s \xC2\xB7 hos deg %s (%d min)", "Leaves %s at %s \xC2\xB7 at your stop %s (%d min)"), j.calls[0].name, dep, at, mins);
    } else if (away == 0 && j.vehicle == j.here) {
        snprintf(big, nb, TR("Ved ditt stopp nå", "At your stop now"));
        snprintf(small, ns, TR("Står ved %s", "Standing at %s"), here.name);
    } else if (away < 0) {
        snprintf(big, nb, TR("Har passert", "Has passed"));
        snprintf(small, ns, TR("Gikk fra %s kl. %s", "Left %s at %s"), here.name, at);
    } else if (away == 0) {
        snprintf(big, nb, mins <= 1 ? TR("Ankommer nå", "Arriving now") : TR("Neste stopp er ditt", "Your stop is next"));
        snprintf(small, ns, TR("Forrige: %s \xC2\xB7 hos deg %s (%d min)%s", "Last: %s \xC2\xB7 at your stop %s (%d min)%s"), j.calls[j.vehicle].name, at, mins, est);
    } else {
        snprintf(big, nb, TR("%d stopp \xC2\xB7 %d min", "%d stops \xC2\xB7 %d min"), away, mins);
        snprintf(small, ns, TR("Ved %s \xC2\xB7 hos deg kl. %s%s", "At %s \xC2\xB7 at your stop %s%s"), j.calls[j.vehicle].name, at, est);
    }
}

void ui_track_summary(char* out, size_t n) {
    out[0] = 0;
    if (!entur_track_configured()) return;
    const Settings& s = settings_get();
    entur_lock();
    const Tracker& t = entur_tracker();
    if (t.count) {
        const Journey& j = t.trips[0];
        int away = entur_track_stops_away(j);
        long sec = (long)(j.calls[j.here].expected - time(nullptr));
        int mins = sec > 0 ? (int)(sec / 60) : 0;
        if (j.vehicle < 0) snprintf(out, n, TR("%s: ikke startet \xC2\xB7 %d min", "%s: not started \xC2\xB7 %d min"), s.track_line, mins);
        else if (away == 0) snprintf(out, n, TR("%s: neste er ditt \xC2\xB7 %d min", "%s: yours is next \xC2\xB7 %d min"), s.track_line, mins);
        else if (away > 0) snprintf(out, n, TR("%s: %d stopp unna \xC2\xB7 %d min", "%s: %d stops \xC2\xB7 %d min"), s.track_line, away, mins);
    }
    entur_unlock();
}

// ── draw ─────────────────────────────────────────────────────────────────────
static void add_call_row(const Journey& j, int i, const Theme& t) {
    const JourneyCall& c = j.calls[i];
    bool passed = i <= j.vehicle, here = i == j.here;
    lv_obj_t* row = ui_plain(g_strip);
    lv_obj_set_size(row, DISPLAY_WIDTH, here ? 30 : 22);
    if (here) { lv_obj_set_style_bg_color(row, tc(t.row_alt), 0); lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0); }
    uint32_t col = passed ? t.dim : t.text;
    char tb[8]; hhmm(c.expected, tb, sizeof(tb));
    lv_obj_t* tl = ui_label(row, here ? &font_b16 : &font_m14, col, tb);
    lv_obj_align(tl, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t* ln = ui_plain(row);
    lv_obj_set_size(ln, 4, here ? 30 : 22);
    lv_obj_set_pos(ln, 70, 0);
    lv_obj_set_style_bg_color(ln, tc(passed ? t.line : t.accent), 0);
    lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
    lv_obj_t* dot = ui_plain(row);
    int ds = here ? 16 : 10;
    lv_obj_set_size(dot, ds, ds);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 72 - ds / 2, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, tc(here ? t.accent : t.bg), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dot, tc(passed ? t.line : t.text), 0);
    lv_obj_set_style_border_width(dot, 2, 0);
    lv_obj_t* nl = ui_label(row, here ? &font_b16 : &font_m14, col, c.name);
    ui_one_line(nl, DISPLAY_WIDTH - 96);
    lv_obj_align(nl, LV_ALIGN_LEFT_MID, 88, 0);
}
static void add_vehicle_row(const Journey& j, const Theme& t) {
    lv_obj_t* row = ui_plain(g_strip);
    lv_obj_set_size(row, DISPLAY_WIDTH, 22);
    lv_obj_t* ln = ui_plain(row);
    lv_obj_set_size(ln, 4, 22);
    lv_obj_set_pos(ln, 70, 0);
    lv_obj_set_style_bg_color(ln, tc(t.accent), 0);
    lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
    lv_obj_t* pill = ui_plain(row);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, 20);
    lv_obj_set_style_pad_hor(pill, 8, 0);
    lv_obj_set_style_radius(pill, 10, 0);
    lv_obj_set_style_bg_color(pill, tc(t.accent), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_align(pill, LV_ALIGN_LEFT_MID, 50, 0);
    char txt[40]; snprintf(txt, sizeof(txt), LV_SYMBOL_DOWN " %s %s", j.line, j.estimated ? TR("(beregnet)", "(est.)") : TR("er her", "is here"));
    lv_obj_t* l = ui_label(pill, &font_b16, t.dark ? 0x000000 : 0xFFFFFF, txt);
    lv_obj_center(l);
}

void ui_track_refresh() {
    const Theme& t = theme();
    const Settings& s = settings_get();
    time_t now = time(nullptr);
    char big[64], small[128], foot[80] = "";
    lv_obj_clean(g_strip);
    if (!entur_track_configured()) {
        lv_obj_add_flag(g_badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(g_big, 8);
        lv_label_set_text(g_big, TR("Følg en linje", "Track a line"));
        lv_label_set_text(g_small, s.stop_count ? TR("Velg stopp og linje over. Du ser hvor bussen er og hvor mange stopp den har igjen til deg.", "Pick a stop and a line above to see where the bus is and how many stops it has left to you.")
                                                : TR("Legg til et stopp først (Stopp-siden).", "Add a stop first (Stops page)."));
        lv_label_set_text(g_foot, "");
        return;
    }
    lv_obj_clear_flag(g_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(g_big, 70);
    entur_lock();
    const Tracker& tr = entur_tracker();
    if (!tr.count) {
        entur_unlock();
        ui_badge_set(g_badge, s.track_line, TM_BUS, false, 0, 0);
        lv_label_set_text(g_big, net_pending(NC_TRACK) ? TR("Henter...", "Loading...") : TR("Ingen avganger", "No departures"));
        snprintf(small, sizeof(small), TR("%s mot %s fra %s", "%s to %s from %s"), s.track_line, s.track_dest[0] ? s.track_dest : TR("alle retninger", "any direction"),
                 stop_index_of(s.track_stop) >= 0 ? s.stops[stop_index_of(s.track_stop)].name : "");
        lv_label_set_text(g_small, small);
        lv_label_set_text(g_foot, "");
        return;
    }
    const Journey& j = tr.trips[0];
    ui_badge_set(g_badge, j.line, j.mode, j.has_color, j.color, j.text_color);
    trip_status(j, now, big, sizeof(big), small, sizeof(small));
    lv_label_set_text(g_big, big);
    lv_label_set_text(g_small, small);
    // strip: the vehicle, then the stops before yours, yours and 2 after. A far-away vehicle gets a "⋮ N stops" gap.
    int to = min(j.count - 1, j.here + 2);
    if (j.vehicle < 0 || j.vehicle >= j.here) {
        for (int i = max(0, j.here - 7); i <= to; i++) {
            add_call_row(j, i, t);
            if (i == j.vehicle && i < j.count - 1) add_vehicle_row(j, t);
        }
    } else if (j.here - j.vehicle <= 7) {
        for (int i = j.vehicle; i <= to; i++) {
            add_call_row(j, i, t);
            if (i == j.vehicle) add_vehicle_row(j, t);
        }
    } else {
        add_call_row(j, j.vehicle, t);
        add_vehicle_row(j, t);
        int from = j.here - 5;
        lv_obj_t* gap = ui_plain(g_strip);
        lv_obj_set_size(gap, DISPLAY_WIDTH, 20);
        lv_obj_t* gl = ui_plain(gap);
        lv_obj_set_size(gl, 4, 20); lv_obj_set_pos(gl, 70, 0);
        lv_obj_set_style_bg_color(gl, tc(t.accent), 0); lv_obj_set_style_bg_opa(gl, LV_OPA_40, 0);
        char g[40]; snprintf(g, sizeof(g), TR("%d stopp til", "%d more stops"), from - j.vehicle - 1);
        lv_obj_t* l = ui_label(gap, &font_m12, t.dim, g);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 88, 0);
        for (int i = from; i <= to; i++) add_call_row(j, i, t);
    }
    if (tr.count > 1) {
        const Journey& j2 = tr.trips[1];
        char at[8]; hhmm(j2.calls[j2.here].expected, at, sizeof(at));
        long sec = (long)(j2.calls[j2.here].expected - now);
        snprintf(foot, sizeof(foot), TR("Neste %s: kl. %s (%ld min)", "Next %s: %s (%ld min)"), j2.line, at, sec > 0 ? sec / 60 : 0);
    }
    entur_unlock();
    lv_label_set_text(g_foot, foot);
    lv_obj_update_layout(g_strip);
    // keep the user's stop in view
    lv_obj_t* last = lv_obj_get_child(g_strip, -1);
    if (last && lv_obj_get_y(last) + lv_obj_get_height(last) > STRIP_H) lv_obj_scroll_to_y(g_strip, lv_obj_get_y(last) + lv_obj_get_height(last) - STRIP_H, LV_ANIM_OFF);
}

void ui_track_follow(const char* stop_id, const char* line, const char* dest) {
    Settings& s = settings_get();
    if (stop_index_of(stop_id) < 0) return;
    strlcpy(s.track_stop, stop_id, sizeof(s.track_stop));
    strlcpy(s.track_line, line, sizeof(s.track_line));
    strlcpy(s.track_dest, dest, sizeof(s.track_dest));
    settings_save();
    entur_lock(); entur_tracker().count = 0; entur_unlock();
    lv_dropdown_set_selected(g_dd_stop, stop_index_of(stop_id));
    fill_lines();
    request_track();
    ui_main_goto_tab(PAGE_TRACK);
}
void ui_track_request() { request_track(); }
