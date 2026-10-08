#include "mqtt.h"
#include "settings.h"
#include "entur.h"
#include "net_task.h"
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

static WiFiClient   g_net;
static PubSubClient g_mq(g_net);
static uint32_t     g_last_try = 0, g_last_pub = 0;
static bool         g_discovered = false;
static char         g_host[64], g_user[32], g_pass[32]; static int g_port = 1883;
static char         g_devid[24];

static bool parse_uri() {
    const Settings& s = settings_get();
    const char* u = s.mqtt_uri;
    if (strncmp(u, "mqtt://", 7)) return false;
    u += 7;
    const char* at = strrchr(u, '@');
    g_user[0] = g_pass[0] = 0;
    if (at) {
        char cred[64]; strlcpy(cred, u, min((size_t)(at - u + 1), sizeof(cred)));
        char* colon = strchr(cred, ':');
        if (colon) { *colon = 0; strlcpy(g_pass, colon + 1, sizeof(g_pass)); }
        strlcpy(g_user, cred, sizeof(g_user));
        u = at + 1;
    }
    strlcpy(g_host, u, sizeof(g_host));
    char* colon = strchr(g_host, ':');
    g_port = 1883;
    if (colon) { *colon = 0; g_port = atoi(colon + 1); }
    return g_host[0] != 0;
}

static void discovery() {
    char topic[96], payload[420];
    struct { const char* id; const char* name; const char* unit; const char* tpl; const char* icon; } sensors[] = {
        {"next",      "Next departure",          "",    "{{ value_json.next }}",        "mdi:bus-clock"},
        {"next_min",  "Next departure minutes",  "min", "{{ value_json.next_min }}",    "mdi:timer-sand"},
        {"next2",     "Second departure",        "",    "{{ value_json.next2 }}",       "mdi:bus-clock"},
        {"next2_min", "Second departure minutes","min", "{{ value_json.next2_min }}",   "mdi:timer-sand"},
        {"stop",      "Stop",                    "",    "{{ value_json.stop }}",        "mdi:bus-stop"},
        {"disruptions","Disruptions",            "",    "{{ value_json.disruptions }}", "mdi:alert"},
    };
    for (auto& se : sensors) {
        snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s/config", g_devid, se.id);
        snprintf(payload, sizeof(payload),
            "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"stat_t\":\"esp32busboard/%s/state\",\"val_tpl\":\"%s\",%s%s%s\"ic\":\"%s\","
            "\"dev\":{\"ids\":[\"%s\"],\"name\":\"ESP32 Bus Board\",\"mf\":\"frogswiper\",\"mdl\":\"JC4827W543\",\"sw\":\"" FW_VERSION "\"}}",
            se.name, g_devid, se.id, g_devid, se.tpl, se.unit[0] ? "\"unit_of_meas\":\"" : "", se.unit, se.unit[0] ? "\"," : "", se.icon, g_devid);
        g_mq.publish(topic, payload, true);
    }
    g_discovered = true;
}

bool mqtt_connected() { return g_mq.connected(); }

static void json_str(char* out, size_t n, const char* in) {   // minimal JSON string escaping
    size_t o = 0;
    for (const char* p = in; *p && o + 2 < n; p++) { if (*p == '"' || *p == '\\') out[o++] = '\\'; out[o++] = *p; }
    out[o] = 0;
}

void mqtt_publish_state() {
    if (!g_mq.connected()) return;
    const Settings& s = settings_get();
    time_t now = time(nullptr);
    char nx[2][64] = {"-", "-"}; int mins[2] = {-1, -1}; int found = 0;
    entur_lock();
    const DepState& st = entur_state();
    for (int i = 0; i < st.count && found < 2; i++) {
        const Departure& d = st.deps[i];
        if (d.cancelled || !entur_visible(d, now, s.board_mode == BOARD_MERGED, s.cur_stop)) continue;
        char tmp[64]; snprintf(tmp, sizeof(tmp), "%s %s", d.line, d.dest);
        json_str(nx[found], sizeof(nx[found]), tmp);
        mins[found] = entur_minutes(d, now);
        found++;
    }
    int sits = st.sit_count;
    entur_unlock();
    char stop[64] = "-";
    if (s.stop_count) json_str(stop, sizeof(stop), s.stops[s.cur_stop < s.stop_count ? s.cur_stop : 0].name);
    char payload[360];
    snprintf(payload, sizeof(payload), "{\"next\":\"%s\",\"next_min\":%d,\"next2\":\"%s\",\"next2_min\":%d,\"stop\":\"%s\",\"disruptions\":%d}",
             nx[0], mins[0], nx[1], mins[1], stop, sits);
    char topic[64]; snprintf(topic, sizeof(topic), "esp32busboard/%s/state", g_devid);
    g_mq.publish(topic, payload, false);
    g_last_pub = millis();
}

void mqtt_loop() {
    const Settings& s = settings_get();
    if (!s.mqtt_uri[0] || !net_wifi_connected()) return;
    if (!g_devid[0]) { uint64_t mac = ESP.getEfuseMac(); snprintf(g_devid, sizeof(g_devid), "esp32busboard_%06llx", mac & 0xFFFFFFULL); }
    if (!g_mq.connected()) {
        if (millis() - g_last_try < 15000) return;
        g_last_try = millis();
        if (!parse_uri()) return;
        g_mq.setServer(g_host, g_port);
        g_mq.setBufferSize(512);
        bool ok = g_user[0] ? g_mq.connect(g_devid, g_user, g_pass) : g_mq.connect(g_devid);
        Serial.printf("[MQTT] connect %s:%d -> %s\n", g_host, g_port, ok ? "ok" : "failed");
        if (ok) { g_discovered = false; discovery(); mqtt_publish_state(); }
        return;
    }
    g_mq.loop();
    if (millis() - g_last_pub > 60000) mqtt_publish_state();
}
