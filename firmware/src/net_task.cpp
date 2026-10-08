#include "net_task.h"
#include "settings.h"
#include "entur.h"
#include "ntp.h"
#include "notify.h"
#include "i18n.h"
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

static QueueHandle_t     g_q      = nullptr;
static SemaphoreHandle_t g_mtx    = nullptr;
static volatile uint32_t g_events = 0;
static volatile bool     g_busy   = false;
static uint32_t          g_pending = 0;     // bit per NetCmd queued/running (atomic: set on core 1, cleared on core 0)
static char              g_status[96] = "Starting...";
static LocalInfo         g_local = {};

static void set_status(const char* s) {
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    strlcpy(g_status, s, sizeof(g_status));
    g_events |= EV_STATUS;
    xSemaphoreGive(g_mtx);
    Serial.printf("[NET] %s\n", s);
}
static void post(uint32_t ev) {
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    g_events |= ev;
    xSemaphoreGive(g_mtx);
}

static bool do_connect_wifi() {
    Settings& s = settings_get();
    if (!strlen(s.wifi_ssid)) { set_status(TR("Mangler WiFi-oppsett", "No WiFi credentials")); return false; }
    char m[96]; snprintf(m, sizeof(m), TR("Kobler til %s...", "Connecting to %s..."), s.wifi_ssid); set_status(m);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setHostname("esp32-busboard");
    WiFi.disconnect(true, false);
    delay(100);
    WiFi.begin(s.wifi_ssid, s.wifi_password);
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(m, sizeof(m), "WiFi OK  %s", WiFi.localIP().toString().c_str());
        set_status(m);
        if (!ntp_is_synced()) ntp_sync();
        return true;
    }
    set_status(TR("WiFi feilet - sjekk navn/passord", "WiFi failed - check SSID/password"));
    return false;
}

static void net_task(void*) {
    NetMsg msg;
    for (;;) {
        if (xQueueReceive(g_q, &msg, portMAX_DELAY) != pdTRUE) continue;
        g_busy = true;
        Settings& s = settings_get();
        switch (msg.cmd) {
        case NC_CONNECT_WIFI:
            post(do_connect_wifi() ? EV_WIFI_OK : EV_WIFI_FAIL);
            break;

        case NC_LOCATE_IP: {
            set_status(TR("Finner posisjon fra IP...", "Locating by public IP..."));
            float lat, lon; char label[48];
            if (geo_locate_by_ip(lat, lon, label, sizeof(label))) {
                s.lat = lat; s.lon = lon;
                strlcpy(s.place, label, sizeof(s.place));
                settings_save();
                char m[96]; snprintf(m, sizeof(m), TR("Posisjon: %s", "Location: %s"), s.place); set_status(m);
                post(EV_LOCATED);
            } else { set_status(TR("IP-posisjon feilet", "IP geolocation failed")); post(EV_LOCATE_FAIL); }
            break;
        }
        case NC_LOCAL_INFO: {
            float lat, lon; settings_weather_position(lat, lon);
            LocalInfo li = {};
            if ((lat || lon) && geo_fetch_local_info(lat, lon, li)) {
                xSemaphoreTake(g_mtx, portMAX_DELAY); g_local = li; xSemaphoreGive(g_mtx);
                post(EV_LOCAL_INFO);
            }
            if (!ntp_is_synced()) ntp_sync();
            break;
        }
        case NC_DEPARTURES:
            post(entur_fetch_departures() ? EV_DEPS : EV_DEPS_FAIL);
            break;
        case NC_JOURNEY:
            entur_fetch_journey(msg.text, msg.arg1, msg.arg2, (time_t)msg.f1);
            post(EV_JOURNEY);
            break;
        case NC_STOP_SEARCH: {
            float lat, lon; settings_weather_position(lat, lon);
            if (!entur_search(msg.text, lat, lon)) { entur_lock(); entur_hits().ok = false; entur_hits().count = 0; entur_unlock(); }
            post(EV_HITS);
            break;
        }
        case NC_STOP_NEARBY: {
            float lat = msg.f1, lon = msg.f2;
            if (!lat && !lon) {
                // "near me": manual position if set, else the IP position (stop coordinates would only find that stop)
                if (s.loc_mode == LOC_MANUAL && (s.man_lat || s.man_lon)) { lat = s.man_lat; lon = s.man_lon; }
                else {
                    if (!s.lat && !s.lon) {
                        float la, lo; char label[48];
                        if (geo_locate_by_ip(la, lo, label, sizeof(label))) { s.lat = la; s.lon = lo; strlcpy(s.place, label, sizeof(s.place)); settings_save(); }
                    }
                    lat = s.lat; lon = s.lon;
                }
            }
            if (!entur_nearby(lat, lon)) { entur_lock(); entur_hits().ok = false; entur_hits().count = 0; entur_unlock(); }
            post(EV_HITS);
            break;
        }
        case NC_TRACK:
            entur_fetch_track();
            post(EV_TRACK);
            break;
        case NC_NOTIFY:
            notify_deliver(msg.arg1, msg.text, (NotifyKind)msg.kind);
            break;
        }
        __atomic_and_fetch(&g_pending, ~(1u << msg.cmd), __ATOMIC_SEQ_CST);
        g_busy = false;
    }
}

void net_task_start() {
    g_q   = xQueueCreate(10, sizeof(NetMsg));
    g_mtx = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(net_task, "net", 20480, nullptr, 1, nullptr, 0);
}

bool net_send(NetCmd cmd, const char* a1, const char* a2, double f1, double f2, const char* text) {
    NetMsg m = {}; m.cmd = cmd; m.f1 = f1; m.f2 = f2;
    if (a1) strlcpy(m.arg1, a1, sizeof(m.arg1));
    if (a2) strlcpy(m.arg2, a2, sizeof(m.arg2));
    if (text) strlcpy(m.text, text, sizeof(m.text));
    __atomic_or_fetch(&g_pending, 1u << cmd, __ATOMIC_SEQ_CST);     // set before queueing so the task can't clear it first
    if (xQueueSend(g_q, &m, 0) != pdTRUE) { __atomic_and_fetch(&g_pending, ~(1u << cmd), __ATOMIC_SEQ_CST); return false; }
    return true;
}
bool net_send_notify(const char* title, const char* message, uint8_t kind) {
    NetMsg m = {}; m.cmd = NC_NOTIFY; m.kind = kind;
    strlcpy(m.arg1, title, sizeof(m.arg1)); strlcpy(m.text, message, sizeof(m.text));
    return xQueueSend(g_q, &m, 0) == pdTRUE;
}
bool net_busy()               { return g_busy; }
bool net_pending(NetCmd cmd)  { return __atomic_load_n(&g_pending, __ATOMIC_SEQ_CST) & (1u << cmd); }
uint32_t net_take_events() {
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    uint32_t e = g_events; g_events = 0;
    xSemaphoreGive(g_mtx);
    return e;
}
void net_status(char* buf, size_t n) {
    xSemaphoreTake(g_mtx, portMAX_DELAY); strlcpy(buf, g_status, n); xSemaphoreGive(g_mtx);
}
bool net_wifi_connected() { return WiFi.status() == WL_CONNECTED; }
int  net_wifi_rssi()      { return WiFi.RSSI(); }
void net_wifi_ip(char* buf, size_t n) { strlcpy(buf, WiFi.localIP().toString().c_str(), n); }
const LocalInfo& net_local_info() { return g_local; }
