#include "settings.h"
#include <Preferences.h>
#include <string.h>
#include <ctype.h>
#include <initializer_list>

static Preferences prefs;
static Settings    g;
static uint32_t    g_stops_ver = 1;
static const char* NS = "bus1";

void settings_load() {
    memset(&g, 0, sizeof(g));
    prefs.begin(NS, true);
    prefs.getString("ssid",  g.wifi_ssid,     sizeof(g.wifi_ssid));
    prefs.getString("pass",  g.wifi_password, sizeof(g.wifi_password));
    prefs.getString("place", g.place,         sizeof(g.place));
    g.loc_mode   = prefs.getUChar("locmode", LOC_AUTO);
    g.lat        = prefs.getFloat("lat", 0.0f);
    g.lon        = prefs.getFloat("lon", 0.0f);
    g.man_lat    = prefs.getFloat("mlat", 0.0f);
    g.man_lon    = prefs.getFloat("mlon", 0.0f);
    if (prefs.getBytesLength("stops") == sizeof(g.stops)) prefs.getBytes("stops", g.stops, sizeof(g.stops));
    g.stop_count = prefs.getUChar("nstops", 0);
    g.cur_stop   = prefs.getUChar("cur", 0);
    g.board_mode = prefs.getUChar("bmode", BOARD_SINGLE);
    g.rotate_s   = prefs.getUChar("rot", 0);
    g.update_s   = prefs.getUShort("upd", 30);
    g.clock_after= prefs.getUChar("clk", 15);
    g.hide_unreachable = prefs.getBool("hideun", false);
    g.show_platform = prefs.getBool("plat", true);
    g.line_colours  = prefs.getBool("lcol", true);
    g.ticker        = prefs.getBool("tick", true);
    g.show_weather  = prefs.getBool("wx", true);
    g.theme      = prefs.getUChar("theme", 0);
    g.lang       = prefs.getUChar("lang", LANG_NO);
    g.brightness = prefs.getUChar("bright", 200);
    g.night_dim  = prefs.getBool("night", true);
    g.night_from = prefs.getUChar("nfrom", 23);
    g.night_to   = prefs.getUChar("nto", 6);
    prefs.getString("tstop", g.track_stop, sizeof(g.track_stop));
    prefs.getString("tline", g.track_line, sizeof(g.track_line));
    prefs.getString("tdest", g.track_dest, sizeof(g.track_dest));
    prefs.getString("alines", g.alert_lines, sizeof(g.alert_lines));
    g.alert_from = prefs.getUChar("afrom", 6);
    g.alert_to   = prefs.getUChar("ato", 10);
    prefs.getString("ntfy",  g.ntfy_topic,  sizeof(g.ntfy_topic));
    prefs.getString("hook",  g.webhook_url, sizeof(g.webhook_url));
    prefs.getString("mqtt",  g.mqtt_uri,    sizeof(g.mqtt_uri));
    prefs.getString("ppass", g.panel_pass,  sizeof(g.panel_pass));
    prefs.end();

    // First boot: inherit WiFi (+ position) from the radar / deskclock firmwares if present.
    for (const char* other : {"ship1", "radar1", "dsk2"}) {
        if (g.wifi_ssid[0]) break;
        if (!prefs.begin(other, true)) continue;
        prefs.getString("ssid", g.wifi_ssid,     sizeof(g.wifi_ssid));
        prefs.getString("pass", g.wifi_password, sizeof(g.wifi_password));
        if (strcmp(other, "dsk2")) {
            prefs.getString("place", g.place, sizeof(g.place));
            g.lat = prefs.getFloat("lat", 0.0f); g.lon = prefs.getFloat("lon", 0.0f);
        }
        prefs.end();
        if (g.wifi_ssid[0]) settings_save();
    }
    if (g.stop_count > MAX_STOPS) g.stop_count = 0;
    for (int i = 0; i < g.stop_count; i++) if (!settings_valid_stop_id(g.stops[i].id)) { g.stop_count = i; break; }
    if (g.cur_stop >= g.stop_count) g.cur_stop = 0;
    if (g.update_s < 15) g.update_s = 15;
    if (g.brightness < 10) g.brightness = 10;
}

void settings_save() {
    prefs.begin(NS, false);
    prefs.putString("ssid",  g.wifi_ssid);
    prefs.putString("pass",  g.wifi_password);
    prefs.putString("place", g.place);
    prefs.putUChar("locmode", g.loc_mode);
    prefs.putFloat("lat",  g.lat);
    prefs.putFloat("lon",  g.lon);
    prefs.putFloat("mlat", g.man_lat);
    prefs.putFloat("mlon", g.man_lon);
    prefs.putBytes("stops", g.stops, sizeof(g.stops));
    prefs.putUChar("nstops", g.stop_count);
    prefs.putUChar("cur",    g.cur_stop);
    prefs.putUChar("bmode",  g.board_mode);
    prefs.putUChar("rot",    g.rotate_s);
    prefs.putUShort("upd",   g.update_s);
    prefs.putUChar("clk",    g.clock_after);
    prefs.putBool("hideun",  g.hide_unreachable);
    prefs.putBool("plat",    g.show_platform);
    prefs.putBool("lcol",    g.line_colours);
    prefs.putBool("tick",    g.ticker);
    prefs.putBool("wx",      g.show_weather);
    prefs.putUChar("theme",  g.theme);
    prefs.putUChar("lang",   g.lang);
    prefs.putUChar("bright", g.brightness);
    prefs.putBool("night",   g.night_dim);
    prefs.putUChar("nfrom",  g.night_from);
    prefs.putUChar("nto",    g.night_to);
    prefs.putString("tstop", g.track_stop);
    prefs.putString("tline", g.track_line);
    prefs.putString("tdest", g.track_dest);
    prefs.putString("alines", g.alert_lines);
    prefs.putUChar("afrom",  g.alert_from);
    prefs.putUChar("ato",    g.alert_to);
    prefs.putString("ntfy",  g.ntfy_topic);
    prefs.putString("hook",  g.webhook_url);
    prefs.putString("mqtt",  g.mqtt_uri);
    prefs.putString("ppass", g.panel_pass);
    prefs.end();
}

Settings& settings_get()     { return g; }
bool settings_has_wifi()     { return strlen(g.wifi_ssid) > 0; }
bool settings_has_location() { float la, lo; settings_weather_position(la, lo); return la != 0.0f || lo != 0.0f; }
uint32_t settings_stops_version() { return g_stops_ver; }

void settings_weather_position(float& lat, float& lon) {
    if (g.loc_mode == LOC_MANUAL && (g.man_lat || g.man_lon)) { lat = g.man_lat; lon = g.man_lon; return; }
    if (g.stop_count > 0 && (g.stops[0].lat || g.stops[0].lon)) { lat = g.stops[0].lat; lon = g.stops[0].lon; return; }
    lat = g.lat; lon = g.lon;
}

bool settings_valid_stop_id(const char* id) {
    if (!id || strncmp(id, "NSR:StopPlace:", 14) || strlen(id) < 15 || strlen(id) >= sizeof(g.stops[0].id)) return false;
    for (const char* p = id + 14; *p; p++) if (!isdigit((unsigned char)*p)) return false;
    return true;
}

void settings_stops_changed() {
    if (g.cur_stop >= g.stop_count) g.cur_stop = 0;
    settings_save();
    g_stops_ver++;
}

bool settings_add_stop(const char* id, const char* name, float lat, float lon) {
    if (!settings_valid_stop_id(id)) return false;
    for (int i = 0; i < g.stop_count; i++) if (!strcmp(g.stops[i].id, id)) return false;
    if (g.stop_count >= MAX_STOPS) return false;
    StopCfg& s = g.stops[g.stop_count];
    memset(&s, 0, sizeof(s));
    strlcpy(s.id, id, sizeof(s.id)); strlcpy(s.name, name, sizeof(s.name));
    s.lat = lat; s.lon = lon;
    g.stop_count++;
    settings_stops_changed();
    return true;
}
void settings_remove_stop(int idx) {
    if (idx < 0 || idx >= g.stop_count) return;
    for (int i = idx; i < g.stop_count - 1; i++) g.stops[i] = g.stops[i + 1];
    g.stop_count--;
    memset(&g.stops[g.stop_count], 0, sizeof(StopCfg));
    settings_stops_changed();
}
void settings_move_stop_up(int idx) {
    if (idx <= 0 || idx >= g.stop_count) return;
    StopCfg t = g.stops[idx - 1]; g.stops[idx - 1] = g.stops[idx]; g.stops[idx] = t;
    settings_stops_changed();
}

void settings_factory_reset() {
    char ssid[64], pass[64];
    strlcpy(ssid, g.wifi_ssid, sizeof(ssid)); strlcpy(pass, g.wifi_password, sizeof(pass));
    prefs.begin(NS, false); prefs.clear(); prefs.end();
    settings_load();
    strlcpy(g.wifi_ssid, ssid, sizeof(g.wifi_ssid)); strlcpy(g.wifi_password, pass, sizeof(g.wifi_password));
    settings_save();
    g_stops_ver++;
}
