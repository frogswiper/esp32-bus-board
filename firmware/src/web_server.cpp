#include "web_server.h"
#include "settings.h"
#include "entur.h"
#include "net_task.h"
#include "notify.h"
#include "ntp.h"
#include "display.h"
#include "ui/ui_main.h"
#include "ui/ui_board.h"
#include "ui/ui_stops.h"
#include "ui/ui_track.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_system.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include "net_util.h"
#include "web_page.h"

static WebServer g_srv(80);
static bool      g_started = false;
static uint32_t  g_ota_until = 0;
static const char* HOSTNAME = "esp32-busboard";

const char* web_server_hostname() { return HOSTNAME; }
bool web_server_started() { return g_started; }
void web_server_arm_ota(uint32_t seconds) { g_ota_until = millis() + seconds * 1000UL; }
bool web_server_ota_armed() { return g_ota_until && (int32_t)(g_ota_until - millis()) > 0; }

static bool auth() {
    const Settings& s = settings_get();
    if (!s.panel_pass[0]) return true;
    if (g_srv.authenticate("admin", s.panel_pass)) return true;
    g_srv.requestAuthentication(BASIC_AUTH, HOSTNAME);
    return false;
}

// Serialize into PSRAM (a String would not fit internal RAM for big states), then send in one piece.
static PsramBuffer g_json;
static void send_json(JsonDocument& doc) {
    g_json.clear();
    serializeJson(doc, g_json);
    g_srv.sendHeader("Access-Control-Allow-Origin", "*");
    g_srv.setContentLength(g_json.size());
    g_srv.send(200, "application/json", "");
    g_srv.sendContent(g_json.data(), g_json.size());
}

static const char* mode_key(uint8_t m) {
    static const char* k[] = {"bus", "tram", "metro", "rail", "water", "coach", "air", "other"};
    return k[m > TM_OTHER ? TM_OTHER : m];
}
static void hhmm(time_t t, char* out, size_t n) { struct tm tm; localtime_r(&t, &tm); snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min); }

static void handle_state() {
    if (!auth()) return;
    const Settings& s = settings_get();
    JsonDocument doc(&g_psram_alloc);
    time_t now = time(nullptr);
    JsonArray stops = doc["stops"].to<JsonArray>();
    for (int i = 0; i < s.stop_count; i++) { JsonObject o = stops.add<JsonObject>(); o["id"] = s.stops[i].id; o["name"] = s.stops[i].name; }
    doc["board_mode"] = s.board_mode; doc["cur_stop"] = s.cur_stop;
    const LocalInfo& li = net_local_info();
    JsonObject wx = doc["weather"].to<JsonObject>();
    wx["valid"] = li.valid; wx["temp"] = li.temp_c; wx["cond"] = geo_wmo_short(li.wmo_code); wx["wind"] = li.wind_mps; wx["gust"] = li.gust_mps;
    entur_lock();
    const DepState& st = entur_state();
    JsonObject f = doc["fetch"].to<JsonObject>();
    f["ok"] = st.ok; f["http"] = st.http; f["error"] = st.error; f["age_s"] = st.fetched_ms ? (millis() - st.fetched_ms) / 1000 : -1;
    f["took_ms"] = st.took_ms; f["count"] = st.count; f["requests"] = st.fetch_count; f["failures"] = st.fail_count; f["bytes"] = st.bytes;
    JsonArray sits = doc["situations"].to<JsonArray>();
    for (int i = 0; i < st.sit_count; i++) sits.add(st.situations[i]);
    JsonArray arr = doc["departures"].to<JsonArray>();
    bool merged = s.board_mode == BOARD_MERGED;
    for (int i = 0; i < st.count; i++) {
        const Departure& d = st.deps[i];
        if (d.expected < now - 20) continue;
        JsonObject o = arr.add<JsonObject>();
        o["stop"] = d.stop; o["stop_id"] = d.stop < st.stop_n ? st.stop_ids[d.stop] : ""; o["stop_name"] = d.stop < st.stop_n ? st.stop_names[d.stop] : "";
        o["line"] = d.line; o["mode"] = mode_key(d.mode); o["dest"] = d.dest; o["quay"] = d.quay;
        o["aimed"] = (long)d.aimed; o["expected"] = (long)d.expected; o["realtime"] = d.realtime; o["cancelled"] = d.cancelled;
        if (d.has_color) { char c[8]; snprintf(c, sizeof(c), "%06lX", (unsigned long)d.color); o["color"] = c; snprintf(c, sizeof(c), "%06lX", (unsigned long)d.text_color); o["text_color"] = c; }
        o["situation"] = d.situation; o["minutes"] = entur_minutes(d, now);
        char tb[16]; entur_format_time(d, now, tb, sizeof(tb)); o["time_text"] = tb;
        hhmm(d.aimed, tb, sizeof(tb)); o["aimed_hm"] = tb;
        o["delay_min"] = (int)((d.expected - d.aimed) / 60);
        o["leave_now"] = entur_leave_now(d, now);
        o["visible"] = entur_visible(d, now, merged, s.cur_stop);
        o["sj"] = d.sj;
    }
    JsonObject tk = doc["track"].to<JsonObject>();
    tk["line"] = s.track_line; tk["dest"] = s.track_dest; tk["stop"] = s.track_stop; tk["active"] = entur_track_configured();
    JsonArray trips = tk["trips"].to<JsonArray>();
    const Tracker& tr = entur_tracker();
    for (int i = 0; i < tr.count; i++) {
        const Journey& j = tr.trips[i];
        JsonObject o = trips.add<JsonObject>();
        o["stops_away"] = entur_track_stops_away(j); o["started"] = j.vehicle >= 0; o["estimated"] = j.estimated;
        o["at"] = j.vehicle >= 0 ? j.calls[j.vehicle].name : ""; o["minutes"] = (long)(j.calls[j.here].expected - now) / 60;
        char hm[8]; hhmm(j.calls[j.here].expected, hm, sizeof(hm)); o["arrives"] = hm;
        JsonArray calls = o["calls"].to<JsonArray>();
        for (int k = max(0, j.here - 8); k <= j.here; k++) {
            JsonObject c = calls.add<JsonObject>(); c["name"] = j.calls[k].name; hhmm(j.calls[k].expected, hm, sizeof(hm)); c["time"] = hm; c["passed"] = k <= j.vehicle;
        }
    }
    entur_unlock();
    doc["uptime_s"] = millis() / 1000; doc["fw"] = FW_VERSION; doc["reset_reason"] = (int)esp_reset_reason(); doc["rssi"] = WiFi.RSSI();
    doc["heap_kb"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    send_json(doc);
}

static void handle_stops_get() {
    if (!auth()) return;
    const Settings& s = settings_get();
    JsonDocument doc(&g_psram_alloc);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < s.stop_count; i++) {
        const StopCfg& c = s.stops[i];
        JsonObject o = arr.add<JsonObject>();
        o["id"] = c.id; o["name"] = c.name; o["lat"] = c.lat; o["lon"] = c.lon; o["lines"] = c.lines; o["quays"] = c.quays; o["walk_min"] = c.walk_min;
    }
    send_json(doc);
}
static void handle_stops_post() {
    if (!auth()) return;
    JsonDocument doc(&g_psram_alloc);
    if (deserializeJson(doc, g_srv.arg("plain")) || !doc.is<JsonArray>()) { g_srv.send(400, "text/plain", "bad json"); return; }
    StopCfg tmp[MAX_STOPS] = {}; int n = 0;
    for (JsonObject o : doc.as<JsonArray>()) {
        if (n >= MAX_STOPS) break;
        const char* id = o["id"] | "";
        if (!settings_valid_stop_id(id)) { g_srv.send(400, "text/plain", "ugyldig stopp-id"); return; }
        bool dup = false; for (int i = 0; i < n; i++) if (!strcmp(tmp[i].id, id)) dup = true;
        if (dup) continue;
        StopCfg& c = tmp[n++];
        strlcpy(c.id, id, sizeof(c.id)); strlcpy(c.name, o["name"] | id, sizeof(c.name));
        c.lat = o["lat"] | 0.0f; c.lon = o["lon"] | 0.0f;
        strlcpy(c.lines, o["lines"] | "", sizeof(c.lines)); strlcpy(c.quays, o["quays"] | "", sizeof(c.quays));
        c.walk_min = constrain((int)(o["walk_min"] | 0), 0, 30);
    }
    Settings& s = settings_get();
    memset(s.stops, 0, sizeof(s.stops));
    memcpy(s.stops, tmp, sizeof(tmp));
    s.stop_count = n;
    settings_stops_changed();
    ui_stops_refresh();
    ui_board_refresh();
    char m[48]; snprintf(m, sizeof(m), "lagret %d stopp", n);
    g_srv.send(200, "text/plain", m);
}

static void config_to_json(JsonDocument& doc) {
    const Settings& s = settings_get();
    doc["wifi_ssid"] = s.wifi_ssid; doc["board_mode"] = s.board_mode; doc["rotate_s"] = s.rotate_s; doc["update_s"] = s.update_s; doc["clock_after"] = s.clock_after;
    doc["hide_unreachable"] = s.hide_unreachable; doc["show_platform"] = s.show_platform; doc["line_colours"] = s.line_colours; doc["ticker"] = s.ticker; doc["show_weather"] = s.show_weather;
    doc["theme"] = s.theme; doc["lang"] = s.lang; doc["brightness"] = s.brightness; doc["night_dim"] = s.night_dim; doc["night_from"] = s.night_from; doc["night_to"] = s.night_to;
    doc["loc_mode"] = s.loc_mode; doc["man_lat"] = s.man_lat; doc["man_lon"] = s.man_lon; doc["place"] = s.place;
    doc["track_stop"] = s.track_stop; doc["track_line"] = s.track_line; doc["track_dest"] = s.track_dest;
    doc["alert_lines"] = s.alert_lines; doc["alert_from"] = s.alert_from; doc["alert_to"] = s.alert_to;
    doc["ntfy_topic"] = s.ntfy_topic; doc["webhook_url"] = s.webhook_url; doc["mqtt_uri"] = s.mqtt_uri[0] ? "(set)" : "";
    doc["panel_pass_set"] = s.panel_pass[0] != 0; doc["ota_armed"] = web_server_ota_armed(); doc["fw"] = FW_VERSION; doc["hostname"] = HOSTNAME;
}
static void handle_config_get() { if (!auth()) return; JsonDocument doc(&g_psram_alloc); config_to_json(doc); send_json(doc); }

static void handle_config_post() {
    if (!auth()) return;
    JsonDocument doc(&g_psram_alloc);
    if (deserializeJson(doc, g_srv.arg("plain"))) { g_srv.send(400, "text/plain", "bad json"); return; }
    Settings& s = settings_get();
    bool reconnect = false, restart = false, weather = false;
    auto str = [&](const char* k, char* dst, size_t n) { if (doc[k].is<const char*>()) strlcpy(dst, doc[k] | "", n); };
    auto u8  = [&](const char* k, uint8_t& dst, int lo, int hi) { if (doc[k].is<int>()) dst = constrain((int)doc[k], lo, hi); };
    auto bl  = [&](const char* k, bool& dst) { if (doc[k].is<bool>()) dst = doc[k]; };
    if (doc["wifi_ssid"].is<const char*>() && strcmp(s.wifi_ssid, doc["wifi_ssid"] | "")) { str("wifi_ssid", s.wifi_ssid, sizeof(s.wifi_ssid)); reconnect = true; }
    if (doc["wifi_password"].is<const char*>() && strlen(doc["wifi_password"] | "")) { str("wifi_password", s.wifi_password, sizeof(s.wifi_password)); reconnect = true; }
    u8("board_mode", s.board_mode, 0, 1); u8("rotate_s", s.rotate_s, 0, 120); u8("clock_after", s.clock_after, 0, 120);
    if (doc["update_s"].is<int>()) s.update_s = constrain((int)doc["update_s"], 15, 600);
    bl("hide_unreachable", s.hide_unreachable); bl("show_platform", s.show_platform); bl("line_colours", s.line_colours); bl("ticker", s.ticker); bl("show_weather", s.show_weather);
    uint8_t th = s.theme, lg = s.lang;
    u8("theme", s.theme, 0, 3); u8("lang", s.lang, 0, 1);
    restart = th != s.theme || lg != s.lang;
    u8("brightness", s.brightness, 10, 255); bl("night_dim", s.night_dim); u8("night_from", s.night_from, 0, 23); u8("night_to", s.night_to, 0, 23);
    uint8_t lm = s.loc_mode; u8("loc_mode", s.loc_mode, 0, 1); if (lm != s.loc_mode) weather = true;
    if (doc["man_lat"].is<float>() && doc["man_lon"].is<float>()) {
        float la = doc["man_lat"], lo = doc["man_lon"];
        if (fabsf(la - s.man_lat) > 1e-5f || fabsf(lo - s.man_lon) > 1e-5f) { s.man_lat = la; s.man_lon = lo; weather = true; }
    }
    bool track = false;
    if (doc["track_line"].is<const char*>()) { str("track_line", s.track_line, sizeof(s.track_line)); track = true; }
    if (doc["track_dest"].is<const char*>()) { str("track_dest", s.track_dest, sizeof(s.track_dest)); track = true; }
    if (doc["track_stop"].is<const char*>() && settings_valid_stop_id(doc["track_stop"] | "")) { str("track_stop", s.track_stop, sizeof(s.track_stop)); track = true; }
    str("alert_lines", s.alert_lines, sizeof(s.alert_lines)); u8("alert_from", s.alert_from, 0, 23); u8("alert_to", s.alert_to, 0, 23);
    str("ntfy_topic", s.ntfy_topic, sizeof(s.ntfy_topic)); str("webhook_url", s.webhook_url, sizeof(s.webhook_url));
    if (doc["mqtt_uri"].is<const char*>() && strcmp(doc["mqtt_uri"] | "", "(set)")) str("mqtt_uri", s.mqtt_uri, sizeof(s.mqtt_uri));
    if (doc["panel_pass"].is<const char*>() && strlen(doc["panel_pass"] | "")) str("panel_pass", s.panel_pass, sizeof(s.panel_pass));
    settings_save();
    display_set_brightness(s.brightness);
    if (restart) { g_srv.send(200, "text/plain", "lagret - starter på nytt"); delay(300); ESP.restart(); }
    if (reconnect) net_send(NC_CONNECT_WIFI);
    if (weather) net_send(NC_LOCAL_INFO);
    if (track) { entur_lock(); entur_tracker().count = 0; entur_unlock(); ui_track_options_changed(); ui_track_request(); }
    ui_board_refresh();
    ui_main_request_fetch();
    g_srv.send(200, "text/plain", reconnect ? "lagret - kobler til WiFi" : "lagret");
}

static void handle_alerts() {
    if (!auth()) return;
    JsonDocument doc(&g_psram_alloc);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < notify_log_count(); i++) {
        const AlertEntry& e = notify_log_get(i);
        JsonObject o = arr.add<JsonObject>();
        char tbuf[24]; time_t t = e.epoch; struct tm tm; localtime_r(&t, &tm);
        if (e.epoch > 1600000000UL) strftime(tbuf, sizeof(tbuf), "%d.%m %H:%M", &tm); else snprintf(tbuf, sizeof(tbuf), "+%lus", (unsigned long)(e.ms / 1000));
        o["time"] = tbuf; o["kind"] = notify_kind_name((NotifyKind)e.kind); o["title"] = e.title; o["message"] = e.message;
    }
    send_json(doc);
}

static void handle_scan() {
    if (!auth()) return;
    int n = WiFi.scanNetworks(false, false, false, 300);
    JsonDocument doc(&g_psram_alloc);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n && i < 30; i++) { JsonObject o = arr.add<JsonObject>(); o["ssid"] = WiFi.SSID(i); o["rssi"] = WiFi.RSSI(i); o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN; }
    WiFi.scanDelete();
    send_json(doc);
}

// 16-bit BMP (BI_BITFIELDS RGB565): the LVGL buffer goes out as-is, 260 KB instead of 390 KB for 24-bit.
static void handle_screen() {
    if (!auth()) return;
    lv_img_dsc_t* snap = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
    if (!snap) { g_srv.send(500, "text/plain", "snapshot failed"); return; }
    int w = snap->header.w, h = snap->header.h;
    uint32_t row = (w * 2 + 3) & ~3, off = 54 + 12, size = off + row * h;
    uint8_t hdr[66] = {'B','M'};
    auto put32 = [&](int o, uint32_t v) { hdr[o] = v; hdr[o+1] = v >> 8; hdr[o+2] = v >> 16; hdr[o+3] = v >> 24; };
    put32(2, size); put32(10, off); put32(14, 40); put32(18, w); put32(22, (uint32_t)(-h)); hdr[26] = 1; hdr[28] = 16;
    put32(30, 3); put32(34, row * h); put32(54, 0xF800); put32(58, 0x07E0); put32(62, 0x001F);
    g_srv.setContentLength(size);
    g_srv.sendHeader("Access-Control-Allow-Origin", "*");
    g_srv.sendHeader("Cache-Control", "no-store");
    g_srv.send(200, "image/bmp", "");
    g_srv.sendContent((const char*)hdr, sizeof(hdr));
    const uint8_t* px = (const uint8_t*)snap->data;
    const size_t chunk = row * 24;                         // ~13 KB per write
    for (size_t o = 0; o < row * h; o += chunk) g_srv.sendContent((const char*)px + o, min(chunk, (size_t)(row * h - o)));
    lv_snapshot_free(snap);
}

static void handle_metrics() {
    if (!auth()) return;
    entur_lock();
    const DepState& st = entur_state();
    int count = st.count, sits = st.sit_count; bool ok = st.ok; uint32_t took = st.took_ms, fc = st.fetch_count, ff = st.fail_count;
    entur_unlock();
    char buf[600];
    snprintf(buf, sizeof(buf),
        "esp32busboard_departures %d\nesp32busboard_disruptions %d\nesp32busboard_entur_ok %d\nesp32busboard_entur_latency_ms %u\n"
        "esp32busboard_entur_requests_total %lu\nesp32busboard_entur_failures_total %lu\nesp32busboard_heap_free_bytes %u\nesp32busboard_heap_min_free_bytes %u\nesp32busboard_reset_reason %d\nesp32busboard_uptime_seconds %lu\nesp32busboard_wifi_rssi %d\n",
        count, sits, ok ? 1 : 0, (unsigned)took, (unsigned long)fc, (unsigned long)ff,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (int)esp_reset_reason(),
        (unsigned long)(millis() / 1000), WiFi.RSSI());
    g_srv.send(200, "text/plain; version=0.0.4", buf);
}

static void handle_action() {
    if (!auth()) return;
    JsonDocument doc; deserializeJson(doc, g_srv.arg("plain"));
    const char* a = doc["action"] | "";
    if (!strcmp(a, "refresh")) { ui_main_request_fetch(); g_srv.send(200, "text/plain", "ok"); }
    else if (!strcmp(a, "locate")) { net_send(NC_LOCATE_IP); g_srv.send(200, "text/plain", "locating"); }
    else if (!strcmp(a, "reboot")) { g_srv.send(200, "text/plain", "rebooting"); delay(200); ESP.restart(); }
    else if (!strcmp(a, "next_stop")) { ui_board_next_stop(1); g_srv.send(200, "text/plain", "ok"); }
    else if (!strcmp(a, "mode")) { Settings& s = settings_get(); s.board_mode ^= 1; settings_save(); ui_board_refresh(); g_srv.send(200, "text/plain", "ok"); }
    else if (!strcmp(a, "page")) { ui_main_goto_tab(doc["page"] | 0); g_srv.send(200, "text/plain", "ok"); }
    else g_srv.send(400, "text/plain", "unknown action");
}

static void handle_ota_done() {
    if (!web_server_ota_armed()) { g_srv.send(403, "text/plain", "OTA locked - arm it on the device (Info page)"); return; }
    bool ok = !Update.hasError();
    g_srv.send(ok ? 200 : 500, "text/plain", ok ? "update ok - rebooting" : "update failed");
    if (ok) { delay(300); ESP.restart(); }
}
static void handle_ota_upload() {
    if (!web_server_ota_armed()) return;
    HTTPUpload& up = g_srv.upload();
    if (up.status == UPLOAD_FILE_START) {
        Serial.printf("[OTA] start %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("[OTA] done, %u bytes\n", up.totalSize); else Update.printError(Serial);
    }
}

void web_server_start() {
    if (g_started) return;
    g_srv.on("/", HTTP_GET, []() { if (!auth()) return; g_srv.send_P(200, "text/html; charset=utf-8", PAGE); });
    g_srv.on("/api/state", HTTP_GET, handle_state);
    g_srv.on("/api/stops", HTTP_GET, handle_stops_get);
    g_srv.on("/api/stops", HTTP_POST, handle_stops_post);
    g_srv.on("/api/config", HTTP_GET, handle_config_get);
    g_srv.on("/api/config", HTTP_POST, handle_config_post);
    g_srv.on("/api/alerts", HTTP_GET, handle_alerts);
    g_srv.on("/api/wifi/scan", HTTP_GET, handle_scan);
    g_srv.on("/api/action", HTTP_POST, handle_action);
    g_srv.on("/screen.bmp", HTTP_GET, handle_screen);
    g_srv.on("/metrics", HTTP_GET, handle_metrics);
    g_srv.on("/ota", HTTP_POST, handle_ota_done, handle_ota_upload);
    g_srv.onNotFound([]() { g_srv.send(404, "text/plain", "not found"); });
    g_srv.begin();
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
    g_started = true;
    Serial.printf("[WEB] panel at http://%s.local/  (%s)\n", HOSTNAME, WiFi.localIP().toString().c_str());
}
void web_server_loop() { if (g_started) g_srv.handleClient(); }
