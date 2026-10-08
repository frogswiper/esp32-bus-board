#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MAX_STOPS 4

enum LocMode   : uint8_t { LOC_AUTO = 0, LOC_MANUAL = 1 };        // weather position: first stop / IP, or fixed
enum BoardMode : uint8_t { BOARD_SINGLE = 0, BOARD_MERGED = 1 };   // one stop at a time, or all stops in one list
enum Lang      : uint8_t { LANG_NO = 0, LANG_EN = 1 };

struct StopCfg {
    char    id[28];          // NSR:StopPlace:16961
    char    name[40];
    float   lat, lon;
    char    lines[48];       // comma-separated public codes to show (blank = all; "-31" hides line 31)
    char    quays[16];       // comma-separated platform codes to show (blank = all)
    uint8_t walk_min;        // walking time to the stop; departures sooner than this are dimmed / hidden
};

struct Settings {
    char     wifi_ssid[64];
    char     wifi_password[64];

    // weather / clock position
    uint8_t  loc_mode;
    char     place[48];
    float    lat, lon;
    float    man_lat, man_lon;

    // stops
    StopCfg  stops[MAX_STOPS];
    uint8_t  stop_count;
    uint8_t  cur_stop;       // stop shown in single mode

    // board
    uint8_t  board_mode;     // BoardMode
    uint8_t  rotate_s;       // single mode: cycle stops every N s (0 = off)
    uint16_t update_s;       // Entur poll period
    uint8_t  clock_after;    // show HH:MM instead of minutes beyond this many minutes
    bool     hide_unreachable;
    bool     show_platform;
    bool     line_colours;   // use operator line colours on badges
    bool     ticker;         // disruption ticker at the bottom
    bool     show_weather;

    // display
    uint8_t  theme;          // index into THEMES
    uint8_t  lang;           // Lang
    uint8_t  brightness;     // 10..255
    bool     night_dim;
    uint8_t  night_from, night_to;   // hours

    // line tracking
    char     track_stop[28];         // stop id (must be one of the saved stops)
    char     track_line[10];         // public code, e.g. "200"
    char     track_dest[44];         // destination text, blank = both directions

    // leave-now alerts / integrations
    char     alert_lines[48];        // lines that trigger a "leave now" push (blank = off)
    uint8_t  alert_from, alert_to;   // active hours, local time
    char     ntfy_topic[48];
    char     webhook_url[96];
    char     mqtt_uri[96];
    char     panel_pass[32];
};

Settings& settings_get();
void      settings_load();
void      settings_save();
void      settings_factory_reset();   // wipe namespace (WiFi kept) and reload defaults
bool      settings_has_wifi();
bool      settings_has_location();
uint32_t  settings_stops_version();   // bumps whenever the stop list changes
void      settings_stops_changed();   // save + bump version
bool      settings_add_stop(const char* id, const char* name, float lat, float lon);
void      settings_remove_stop(int idx);
void      settings_move_stop_up(int idx);
bool      settings_valid_stop_id(const char* id);
void      settings_weather_position(float& lat, float& lon);   // effective position for weather
