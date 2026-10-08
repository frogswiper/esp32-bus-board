#include "ntp.h"
#include <Arduino.h>
#include <time.h>

// The board is Norway-only (Entur), so the clock always runs on Norwegian time with DST rules.
static const char* TZ_NORWAY = "CET-1CEST,M3.5.0,M10.5.0/3";
static bool g_synced = false;

void ntp_init_tz() { setenv("TZ", TZ_NORWAY, 1); tzset(); }

void ntp_sync() {
    configTzTime(TZ_NORWAY, "no.pool.ntp.org", "pool.ntp.org", "time.google.com");
    struct tm info;
    uint32_t start = millis();
    while (!getLocalTime(&info, 1000) && millis() - start < 10000) delay(100);
    g_synced = (millis() - start < 10000);
}

bool ntp_is_synced() { return g_synced || time(nullptr) > 1700000000; }

struct tm ntp_get_local_time() {
    struct tm info = {};
    time_t t = time(nullptr);
    localtime_r(&t, &info);
    return info;
}
