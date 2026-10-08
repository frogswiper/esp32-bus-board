#include "ui_info.h"
#include "ui_main.h"
#include "theme.h"
#include "../board_config.h"
#include "../settings.h"
#include "../entur.h"
#include "../net_task.h"
#include "../ntp.h"
#include "../web_server.h"
#include "../mqtt.h"
#include "../i18n.h"
#include <initializer_list>
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

static lv_obj_t* g_txt = nullptr;

static void refresh_cb(lv_event_t*) { ui_main_request_fetch(); }
static void reboot_cb(lv_event_t*)  { ESP.restart(); }
static void reset_cb(lv_event_t*)   { settings_factory_reset(); ESP.restart(); }
static void ota_cb(lv_event_t*)     { web_server_arm_ota(600); ui_info_refresh(); }

void ui_info_build(lv_obj_t* parent) {
    const Theme& t = theme();
    lv_obj_t* col = lv_obj_create(parent);
    lv_obj_set_size(col, DISPLAY_WIDTH, CONTENT_HEIGHT);
    lv_obj_set_style_bg_color(col, tc(t.bg), 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_radius(col, 0, 0);
    lv_obj_set_style_pad_all(col, 12, 0);
    lv_obj_set_style_pad_row(col, 8, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);

    ui_label(col, &font_b16, t.text, "ESP32 Bus Board " FW_VERSION);
    g_txt = ui_label(col, &font_m12, t.text, "");
    lv_obj_set_width(g_txt, LV_PCT(100));
    lv_label_set_long_mode(g_txt, LV_LABEL_LONG_WRAP);
    for (auto b : {ui_button(col, TR("Oppdater avganger nå", "Refresh departures now"), refresh_cb),
                   ui_button(col, TR("Tillat fastvareoppdatering (10 min)", "Allow firmware update (10 min)"), ota_cb),
                   ui_button(col, TR("Start på nytt", "Reboot"), reboot_cb),
                   ui_button(col, TR("Nullstill oppsett (beholder WiFi)", "Reset settings (keeps WiFi)"), reset_cb)})
        lv_obj_set_width(b, LV_PCT(100));
    lv_obj_t* credit = ui_label(col, &font_m12, t.dim, TR("Data: Entur (NLOD) · vær: Open-Meteo", "Data: Entur (NLOD) · weather: Open-Meteo"));
    lv_label_set_long_mode(credit, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(credit, LV_PCT(100));
}

void ui_info_refresh() {
    if (!g_txt) return;
    const Settings& s = settings_get();
    char st[96]; net_status(st, sizeof(st));
    char ip[20]; net_wifi_ip(ip, sizeof(ip));
    entur_lock();
    const DepState& d = entur_state();
    bool ok = d.ok; int count = d.count, sits = d.sit_count, http = d.http;
    uint32_t took = d.took_ms, fc = d.fetch_count, ff = d.fail_count; size_t bytes = d.bytes;
    uint32_t age = d.fetched_ms ? (millis() - d.fetched_ms) / 1000 : 0;
    char err[64]; strlcpy(err, d.error, sizeof(err));
    entur_unlock();
    struct tm t = ntp_get_local_time();
    char stops[200] = "";
    for (int i = 0; i < s.stop_count; i++) { char l[64]; snprintf(l, sizeof(l), "%s%s", i ? ", " : "", s.stops[i].name); strlcat(stops, l, sizeof(stops)); }
    static char buf[1000];
    snprintf(buf, sizeof(buf),
        "%s: %s\n"
        "Entur: %s, HTTP %d, %u ms, %u KB\n"
        "%s %lus %s, %d %s, %d %s\n"
        "%s %lu / %s %lu\n\n"
        "%s %02d:%02d:%02d (Europe/Oslo)%s\n"
        "WiFi: %s  %s  %d dBm\n"
        "Status: %s\n\n"
        "Web: http://%s.local  (%s)\nOTA: %s   MQTT: %s\n"
        "Heap %u KB   PSRAM %u KB\n"
        "%s %lu min",
        TR("Stopp", "Stops"), stops[0] ? stops : "-",
        ok ? "OK" : (err[0] ? err : "-"), http, (unsigned)took, (unsigned)(bytes / 1024),
        TR("Hentet for", "Fetched"), (unsigned long)age, TR("siden", "ago"), count, TR("avganger", "departures"), sits, TR("avvik", "disruptions"),
        TR("Spørringer", "Requests"), (unsigned long)fc, TR("feil", "failed"), (unsigned long)ff,
        TR("Klokke", "Clock"), t.tm_hour, t.tm_min, t.tm_sec, ntp_is_synced() ? "" : TR(" - ikke synkronisert", " - not synced"),
        net_wifi_connected() ? TR("tilkoblet", "connected") : TR("nede", "down"), ip, net_wifi_connected() ? net_wifi_rssi() : 0,
        st,
        web_server_hostname(), web_server_started() ? ip : "-", web_server_ota_armed() ? TR("ÅPEN", "ARMED") : TR("låst", "locked"),
        s.mqtt_uri[0] ? (mqtt_connected() ? "OK" : "...") : TR("av", "off"),
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
        TR("Oppetid", "Uptime"), (unsigned long)(millis() / 60000));
    lv_label_set_text(g_txt, buf);
}
