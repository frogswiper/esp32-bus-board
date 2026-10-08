#include "notify.h"
#include "settings.h"
#include "net_util.h"
#include "net_task.h"
#include <Arduino.h>
#include <time.h>
#include <string.h>
#include <mbedtls/base64.h>

static AlertEntry g_log[ALERT_LOG_SIZE];
static int g_log_n = 0, g_log_head = 0;

struct Recent { char key[12]; uint8_t kind; uint32_t ms; };
static Recent g_recent[48];
static int g_recent_n = 0;
static const uint32_t REPEAT_MS = 30UL * 60UL * 1000UL;

void notify_init() { memset(g_log, 0, sizeof(g_log)); }

const char* notify_kind_name(NotifyKind k) {
    switch (k) { case NK_LEAVE: return "leave"; case NK_DISRUPTION: return "disruption"; default: return "info"; }
}

static bool rate_limited(NotifyKind kind, const char* key) {
    uint32_t now = millis();
    for (int i = 0; i < g_recent_n; i++)
        if (g_recent[i].kind == kind && !strcmp(g_recent[i].key, key)) {
            if (now - g_recent[i].ms < REPEAT_MS) return true;
            g_recent[i].ms = now; return false;
        }
    int slot = g_recent_n < 48 ? g_recent_n++ : 0;
    if (g_recent_n == 48) { uint32_t oldest = now; for (int i = 0; i < 48; i++) if (g_recent[i].ms < oldest) { oldest = g_recent[i].ms; slot = i; } }
    strlcpy(g_recent[slot].key, key, sizeof(g_recent[slot].key)); g_recent[slot].kind = kind; g_recent[slot].ms = now;
    return false;
}

bool notify_event(NotifyKind kind, const char* key, const char* title, const char* message) {
    if (rate_limited(kind, key)) return false;
    AlertEntry& e = g_log[g_log_head];
    e.epoch = (uint32_t)time(nullptr); e.ms = millis(); e.kind = kind;
    strlcpy(e.title, title, sizeof(e.title)); strlcpy(e.message, message, sizeof(e.message));
    g_log_head = (g_log_head + 1) % ALERT_LOG_SIZE; if (g_log_n < ALERT_LOG_SIZE) g_log_n++;
    Serial.printf("[ALERT] %s: %s - %s\n", notify_kind_name(kind), title, message);
    const Settings& s = settings_get();
    bool push = kind == NK_LEAVE;
    if (push && (s.ntfy_topic[0] || s.webhook_url[0])) net_send_notify(title, message, kind);
    return true;
}

int notify_log_count() { return g_log_n; }
const AlertEntry& notify_log_get(int i) {
    int idx = (g_log_head - 1 - i + ALERT_LOG_SIZE * 2) % ALERT_LOG_SIZE;
    return g_log[idx];
}

static PsramBuffer g_buf;

static void json_escape(const char* in, char* out, size_t n) {
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)in; *p && o + 7 < n; p++) {
        if (*p == '"' || *p == '\\') { out[o++] = '\\'; out[o++] = *p; }
        else if (*p < 0x20) o += snprintf(out + o, n - o, "\\u%04x", *p);
        else out[o++] = *p;
    }
    out[o] = 0;
}

void notify_deliver(const char* title, const char* message, NotifyKind kind) {
    const Settings& s = settings_get();
    if (s.ntfy_topic[0]) {
        char url[96]; snprintf(url, sizeof(url), "https://ntfy.sh/%s", s.ntfy_topic);
        // header values must be ASCII: send the title RFC 2047-encoded (ntfy decodes =?UTF-8?B?...?=)
        unsigned char b64[96]; size_t blen = 0;
        mbedtls_base64_encode(b64, sizeof(b64) - 1, &blen, (const unsigned char*)title, strlen(title)); b64[blen] = 0;
        char htitle[120]; snprintf(htitle, sizeof(htitle), "Title: =?UTF-8?B?%s?=", (const char*)b64);
        char htags[48];  snprintf(htags, sizeof(htags), "Tags: %s", kind == NK_LEAVE ? "bus,runner" : kind == NK_DISRUPTION ? "warning" : "information_source");
        const char* hprio = kind == NK_LEAVE ? "Priority: high" : "Priority: default";
        const char* headers[] = {htitle, htags, hprio, nullptr};
        int rc = http_post_to_buffer(url, message, "text/plain; charset=utf-8", headers, g_buf, 8000);
        Serial.printf("[NTFY] %d\n", rc);
    }
    if (s.webhook_url[0]) {
        char body[400], et[96], em[200];
        json_escape(title, et, sizeof(et)); json_escape(message, em, sizeof(em));
        snprintf(body, sizeof(body), "{\"source\":\"esp32-bus-board\",\"kind\":\"%s\",\"title\":\"%s\",\"message\":\"%s\"}", notify_kind_name(kind), et, em);
        int rc = http_post_to_buffer(s.webhook_url, body, "application/json", nullptr, g_buf, 8000);
        Serial.printf("[HOOK] %d\n", rc);
    }
}
