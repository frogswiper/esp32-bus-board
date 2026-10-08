#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct LocalInfo {          // from Open-Meteo forecast (current weather)
    bool  valid;
    int   utc_offset_seconds;
    float temp_c;
    float wind_mps;
    float gust_mps;
    int   wind_dir;        // degrees from
    int   wmo_code;
    char  tz_name[32];
};

// Public-IP geolocation (ip-api.com, fallback ipwho.is). label like "Oslo, NO".
bool geo_locate_by_ip(float& lat, float& lon, char* label, size_t label_len);
// Current weather for a position.
bool geo_fetch_local_info(float lat, float lon, LocalInfo& out);
// WMO weather code → short text in the UI language
const char* geo_wmo_short(int code);
