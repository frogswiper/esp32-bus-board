#include "geo.h"
#include "net_util.h"
#include "settings.h"
#include <ctype.h>

static PsramBuffer g_buf;

[[maybe_unused]] static void title_case(char* s) {
    bool start = true;
    for (char* p = s; *p; p++) {
        if (isalpha((unsigned char)*p)) { *p = start ? toupper((unsigned char)*p) : tolower((unsigned char)*p); start = false; }
        else start = (*p == ' ' || *p == '-' || *p == '(' || *p == '\'');
    }
}

bool geo_locate_by_ip(float& lat, float& lon, char* label, size_t label_len) {
    // 1) ip-api.com (plain HTTP on the free tier)
    int rc = http_get_to_buffer("http://ip-api.com/json/?fields=status,country,countryCode,city,lat,lon", g_buf, 8000);
    if (rc == 200) {
        JsonDocument doc(&g_psram_alloc);
        if (!deserializeJson(doc, g_buf.data(), g_buf.size()) && !strcmp(doc["status"] | "", "success")) {
            lat = doc["lat"].as<float>(); lon = doc["lon"].as<float>();
            snprintf(label, label_len, "%s, %s", doc["city"] | "?", doc["countryCode"] | "??");
            return true;
        }
    }
    // 2) ipwho.is (HTTPS)
    rc = http_get_to_buffer("https://ipwho.is/?fields=success,country_code,city,latitude,longitude", g_buf, 10000);
    if (rc == 200) {
        JsonDocument doc(&g_psram_alloc);
        if (!deserializeJson(doc, g_buf.data(), g_buf.size()) && (doc["success"] | false)) {
            lat = doc["latitude"].as<float>(); lon = doc["longitude"].as<float>();
            snprintf(label, label_len, "%s, %s", doc["city"] | "?", doc["country_code"] | "??");
            return true;
        }
    }
    return false;
}

bool geo_fetch_local_info(float lat, float lon, LocalInfo& out) {
    char url[200];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code,wind_speed_10m,wind_direction_10m,wind_gusts_10m"
        "&wind_speed_unit=ms&timezone=auto", lat, lon);
    int rc = http_get_to_buffer(url, g_buf, 10000);
    if (rc != 200) return false;
    JsonDocument doc(&g_psram_alloc);
    if (deserializeJson(doc, g_buf.data(), g_buf.size())) return false;
    out.utc_offset_seconds = doc["utc_offset_seconds"] | 0;
    out.temp_c   = doc["current"]["temperature_2m"] | 0.0f;
    out.wmo_code = doc["current"]["weather_code"] | 0;
    out.wind_mps = doc["current"]["wind_speed_10m"] | 0.0f;
    out.wind_dir = doc["current"]["wind_direction_10m"] | 0;
    out.gust_mps = doc["current"]["wind_gusts_10m"] | 0.0f;
    strlcpy(out.tz_name, doc["timezone"] | "", sizeof(out.tz_name));
    out.valid = true;
    return true;
}

const char* geo_wmo_short(int c) {
    bool en = settings_get().lang == LANG_EN;
    if (c == 0) return en ? "Clear" : "Klart";
    if (c <= 2) return en ? "Fair" : "Lettskyet";
    if (c == 3) return en ? "Cloudy" : "Skyet";
    if (c == 45 || c == 48) return en ? "Fog" : "Tåke";
    if (c >= 51 && c <= 57) return en ? "Drizzle" : "Yr";
    if (c >= 61 && c <= 67) return en ? "Rain" : "Regn";
    if (c >= 71 && c <= 77) return en ? "Snow" : "Snø";
    if (c >= 80 && c <= 82) return en ? "Showers" : "Byger";
    if (c >= 85 && c <= 86) return en ? "Snow" : "Snøbyger";
    if (c >= 95) return en ? "Storm" : "Torden";
    return "";
}
