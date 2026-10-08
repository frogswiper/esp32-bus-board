/**
 * ESP32 Bus Board — JC4827W543 (ESP32-S3, 4.3" 272×480 portrait, GT911 touch)
 *
 * Live public-transport departure board for Norway, fed by Entur's open Journey Planner API
 * (every operator: Ruter, Brakar, Skyss, AtB, Kolumbus, Vy, ...). Network I/O runs on core 0
 * (net_task); LVGL runs here on core 1.
 */
#include <Arduino.h>
#include "display.h"
#include "settings.h"
#include "entur.h"
#include "net_task.h"
#include "ntp.h"
#include "i18n.h"
#include "ui/ui_main.h"
#include "ui/ui_board.h"
#include "ui/ui_stops.h"
#include "ui/ui_info.h"
#include "ui/ui_settings.h"
#include "ui/ui_journey.h"
#include "ui/ui_track.h"
#include "web_server.h"
#include "mqtt.h"
#include "notify.h"
#include <lvgl.h>

// Serial debug: S screenshot, 1-5 pages, N nearby, F search, E/Q open/close stop-1 editor, J journey of row 1,
// T track page, M toggle merged view, X next stop, A<stop id> add stop, K keyboard, O theme dropdown, R reboot, D reset.
static void serial_commands() {
    while (Serial.available()) {
        int c = Serial.read();
        if (c == 'S') {
            lv_img_dsc_t* snap = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
            if (!snap) { Serial.println("SNAP failed"); continue; }
            // the top layer (keyboard, modal) is not part of the screen snapshot: blend it in
            lv_img_dsc_t* top = lv_snapshot_take(lv_layer_top(), LV_IMG_CF_TRUE_COLOR_ALPHA);
            size_t n = (size_t)snap->header.w * snap->header.h * sizeof(lv_color_t);
            if (top) {
                lv_color_t* dst = (lv_color_t*)snap->data;
                const uint8_t* src = top->data;
                size_t px = (size_t)snap->header.w * snap->header.h;
                for (size_t i = 0; i < px; i++) {
                    lv_color_t c2; c2.full = src[i * 3] | (src[i * 3 + 1] << 8);
                    uint8_t a = src[i * 3 + 2];
                    if (a) dst[i] = lv_color_mix(c2, dst[i], a);
                }
                lv_snapshot_free(top);
            }
            Serial.printf("SNAP %d %d %u\n", snap->header.w, snap->header.h, (unsigned)n);
            Serial.write(snap->data, n);
            Serial.flush();
            lv_snapshot_free(snap);
        } else if (c == 'R') {
            ESP.restart();
        } else if (c == 'D') {
            settings_factory_reset(); ESP.restart();
        } else if (c == 'A') {           // A<NSR:StopPlace:id>\n → add stop
            char line[40]; size_t n = Serial.readBytesUntil('\n', line, sizeof(line) - 1); line[n] = 0;
            while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
            Serial.printf("add %s -> %d\n", line, settings_add_stop(line, line + 14, 0, 0));
            ui_stops_refresh(); ui_main_request_fetch();
        } else if (c == 'M') {
            Settings& s = settings_get(); s.board_mode ^= 1; settings_save(); ui_board_refresh();
        } else if (c == 'X') {
            ui_board_next_stop(1);
        } else if (c == 'J') {
            entur_lock();
            const DepState& st = entur_state(); time_t now = time(nullptr);
            int idx = -1;
            for (int i = 0; i < st.count; i++) if (entur_visible(st.deps[i], now, settings_get().board_mode == BOARD_MERGED, settings_get().cur_stop)) { idx = i; break; }
            Departure d = {}; char sid[28] = "";
            if (idx >= 0) { d = st.deps[idx]; strlcpy(sid, st.stop_ids[d.stop], sizeof(sid)); }
            entur_unlock();
            if (idx >= 0) ui_journey_open(d, sid);
        } else if (c == 'N' || c == 'F' || c == 'E' || c == 'Q') {
            ui_main_goto_tab(PAGE_STOPS); ui_stops_debug(c);
        } else if (c == 'K' || c == 'O') {
            ui_main_goto_tab(PAGE_SETTINGS); ui_settings_debug(c);
        } else if (c == 'T') {
            ui_main_goto_tab(PAGE_TRACK);
        } else if (c >= '1' && c <= '5') {
            ui_main_goto_tab(c - '1');
        }
    }
}

static uint32_t t_last_fetch = 0, t_last_wx = 0, t_last_wifi = 0, t_last_info = 0, t_last_dim = 0, t_last_tick = 0, t_last_leave = 0;
static int      wifi_backoff_s = 10;
static bool     wifi_was_up = false;
static uint32_t seen_ver = 0, t_last_track = 0;
static bool     want_fetch = false;
static float    wx_lat = 0, wx_lon = 0;

static void apply_brightness() {
    const Settings& s = settings_get();
    uint8_t b = s.brightness;
    if (s.night_dim && ntp_is_synced()) {
        struct tm t = ntp_get_local_time();
        bool night = s.night_from > s.night_to ? (t.tm_hour >= s.night_from || t.tm_hour < s.night_to)
                                               : (t.tm_hour >= s.night_from && t.tm_hour < s.night_to);
        if (night) b = (uint8_t)max(8, (int)(b * 0.2f));
    }
    display_set_brightness(b);
}

// Returns true when the request went out (or there is nothing to fetch).
static bool request_departures() {
    if (settings_get().stop_count == 0) return true;
    if (!net_wifi_connected() || net_pending(NC_DEPARTURES)) return false;
    if (!net_send(NC_DEPARTURES)) return false;
    t_last_fetch = millis();
    return true;
}

// "Leave now" pushes for the configured lines inside the active hours (once per trip).
static void check_leave_alerts() {
    const Settings& s = settings_get();
    if (!s.alert_lines[0] || !ntp_is_synced()) return;
    struct tm lt = ntp_get_local_time();
    bool active = s.alert_from <= s.alert_to ? (lt.tm_hour >= s.alert_from && lt.tm_hour < s.alert_to)
                                             : (lt.tm_hour >= s.alert_from || lt.tm_hour < s.alert_to);
    if (!active) return;
    time_t now = time(nullptr);
    entur_lock();
    const DepState& st = entur_state();
    for (int i = 0; i < st.count; i++) {
        const Departure& d = st.deps[i];
        if (!entur_leave_now(d, now) || !entur_visible(d, now, true, 0)) continue;
        // is the line in the alert list?
        bool listed = false;
        char list[48]; strlcpy(list, s.alert_lines, sizeof(list));
        for (char* tok = strtok(list, ", "); tok; tok = strtok(nullptr, ", ")) if (!strcasecmp(tok, d.line)) listed = true;
        if (!listed) continue;
        char title[40], msg[96], key[12];
        snprintf(title, sizeof(title), TR("Gå nå: %s %s", "Leave now: %s %s"), entur_mode_name(d.mode), d.line);
        char tb[12]; entur_format_time(d, now, tb, sizeof(tb));
        snprintf(msg, sizeof(msg), TR("%s mot %s går %s fra %s", "%s to %s leaves %s from %s"), d.line, d.dest, tb, d.stop < st.stop_n ? st.stop_names[d.stop] : "");
        uint32_t h = 2166136261u; for (const char* p = d.sj; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
        snprintf(key, sizeof(key), "%08lx", (unsigned long)h);
        notify_event(NK_LEAVE, key, title, msg);
    }
    entur_unlock();
}

void setup() {
    Serial.begin(115200);
    Serial.printf("[Bus] ESP32 Bus Board " FW_VERSION " starting (reset reason %d)\n", (int)esp_reset_reason());
    ntp_init_tz();
    settings_load();
    display_init();
    {
        const Settings& s = settings_get();
        Serial.printf("[Bus] settings: ssid='%s' stops=%d mode=%d upd=%d theme=%d lang=%d\n", s.wifi_ssid, s.stop_count, s.board_mode, s.update_s, s.theme, s.lang);
        for (int i = 0; i < s.stop_count; i++) Serial.printf("[Bus]   stop %d: %s %s lines='%s' quays='%s' walk=%d\n", i, s.stops[i].id, s.stops[i].name, s.stops[i].lines, s.stops[i].quays, s.stops[i].walk_min);
    }
    entur_init();
    notify_init();
    net_task_start();
    ui_main_init();
    display_set_brightness(settings_get().brightness);

    if (settings_has_wifi()) {
        net_send(NC_CONNECT_WIFI);
    } else {
        ui_main_goto_tab(PAGE_SETTINGS);
        ui_settings_set_status(TR("Skriv inn WiFi for å komme i gang.", "Enter WiFi to get started."));
    }
}

void loop() {
    lv_timer_handler();
    serial_commands();
    uint32_t now = millis();
    Settings& s = settings_get();

    // ── events from the network task ──
    uint32_t ev = net_take_events();
    if (ev & EV_STATUS) { char st[96]; net_status(st, sizeof(st)); ui_settings_set_status(st); }
    if (ev & EV_WIFI_OK) {
        wifi_was_up = true; wifi_backoff_s = 10;
        web_server_start();
        if (!settings_has_location()) net_send(NC_LOCATE_IP);
        else { net_send(NC_LOCAL_INFO); t_last_wx = now; }
        request_departures();
        ui_board_refresh();
    }
    if (ev & EV_WIFI_FAIL) {
        if (ui_main_active_tab() == PAGE_BOARD && !settings_has_wifi()) ui_main_goto_tab(PAGE_SETTINGS);
    }
    if (ev & EV_LOCATED)    { ui_settings_location_changed(); net_send(NC_LOCAL_INFO); t_last_wx = now; }
    if (ev & EV_LOCAL_INFO) { ui_board_weather_changed(); apply_brightness(); }
    if (ev & (EV_DEPS | EV_DEPS_FAIL)) {
        if (ui_main_active_tab() == PAGE_BOARD) ui_board_refresh();
        if (ev & EV_DEPS) { mqtt_publish_state(); check_leave_alerts(); if (entur_track_configured()) { ui_track_request(); t_last_track = now; } }
        if (ui_main_active_tab() == PAGE_TRACK) ui_track_options_changed();
    }
    if ((ev & EV_JOURNEY) && ui_main_active_tab() == PAGE_JOURNEY) ui_journey_refresh();
    if (ev & EV_HITS) ui_stops_hits_changed();
    if (ev & EV_TRACK) { if (ui_main_active_tab() == PAGE_TRACK) ui_track_refresh(); ui_board_weather_changed(); }

    // ── scheduled work ──
    if (seen_ver != settings_stops_version()) { seen_ver = settings_stops_version(); ui_board_refresh(); want_fetch = true; }
    if (ui_main_consume_fetch_request()) want_fetch = true;
    if (want_fetch && request_departures()) want_fetch = false;
    uint32_t period = (uint32_t)s.update_s * 1000UL;
    if (ui_main_active_tab() != PAGE_BOARD) period *= 2;            // nobody is looking at the board: poll gently
    if (now - t_last_fetch >= period) request_departures();
    // weather: every 30 min, or when the position moves (e.g. first stop changed)
    float la, lo; settings_weather_position(la, lo);
    bool moved = fabsf(la - wx_lat) > 0.01f || fabsf(lo - wx_lon) > 0.01f;
    if (net_wifi_connected() && (la || lo) && (moved || now - t_last_wx >= 30UL * 60UL * 1000UL)) {
        wx_lat = la; wx_lon = lo; net_send(NC_LOCAL_INFO); t_last_wx = now;
        ui_settings_location_changed();
    }

    // WiFi watchdog with backoff
    if (settings_has_wifi() && !net_wifi_connected() && !net_busy() && now - t_last_wifi >= (uint32_t)wifi_backoff_s * 1000UL) {
        t_last_wifi = now;
        if (wifi_was_up || wifi_backoff_s > 10) {
            net_send(NC_CONNECT_WIFI);
            wifi_backoff_s = min(wifi_backoff_s * 2, 120);
        } else wifi_backoff_s = 20;   // first failure: give the user time on the settings page
    }

    web_server_loop();
    mqtt_loop();
    if (now - t_last_tick >= 1000) {
        t_last_tick = now;
        if (ui_main_active_tab() == PAGE_BOARD) ui_board_tick();
        if (ui_main_active_tab() == PAGE_JOURNEY) ui_journey_tick();
        if (ui_main_active_tab() == PAGE_TRACK) {
            if (now - t_last_track >= 20000 && net_wifi_connected()) { ui_track_request(); t_last_track = now; }   // live position every 20 s
            else if (now % 5000 < 1000) ui_track_refresh();                                                      // minutes tick down
        }
    }
    if (ui_main_active_tab() == PAGE_INFO && now - t_last_info >= 2000) { ui_info_refresh(); t_last_info = now; }
    if (now - t_last_leave >= 20000) { check_leave_alerts(); t_last_leave = now; }
    if (now - t_last_dim >= 60000) { apply_brightness(); t_last_dim = now; }

    delay(5);
}
