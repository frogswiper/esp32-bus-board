#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "geo.h"

// Commands executed on the network task (core 0). UI/main loop never block on HTTP.
enum NetCmd : uint8_t {
    NC_CONNECT_WIFI,     // (re)connect using saved credentials
    NC_LOCATE_IP,        // public-IP geolocation → settings lat/lon
    NC_LOCAL_INFO,       // weather for the current position, then NTP
    NC_DEPARTURES,       // poll Entur for every saved stop
    NC_JOURNEY,          // text = serviceJourney id, arg1 = date, arg2 = stop id, f1 = aimed epoch
    NC_STOP_SEARCH,      // text = query
    NC_STOP_NEARBY,      // f1/f2 = lat/lon (0,0 = current position)
    NC_TRACK,            // load the tracked line's next trips (selection already made under entur_lock)
    NC_NOTIFY,           // arg1 = title, text = message, kind
};

struct NetMsg {
    NetCmd  cmd;
    char    arg1[48];
    char    arg2[32];
    double  f1, f2;
    char    text[128];
    uint8_t kind;
};

// Event bits delivered back to the main loop
enum : uint32_t {
    EV_WIFI_OK       = 1 << 0,
    EV_WIFI_FAIL     = 1 << 1,
    EV_LOCATED       = 1 << 2,
    EV_LOCATE_FAIL   = 1 << 3,
    EV_DEPS          = 1 << 4,
    EV_DEPS_FAIL     = 1 << 5,
    EV_JOURNEY       = 1 << 6,
    EV_HITS          = 1 << 7,
    EV_LOCAL_INFO    = 1 << 8,
    EV_STATUS        = 1 << 9,   // status text changed
    EV_TRACK         = 1 << 10,
};

void     net_task_start();
bool     net_send(NetCmd cmd, const char* arg1 = nullptr, const char* arg2 = nullptr, double f1 = 0, double f2 = 0, const char* text = nullptr);
bool     net_send_notify(const char* title, const char* message, uint8_t kind);
bool     net_busy();                       // a command is executing
bool     net_pending(NetCmd cmd);          // a command of this type is queued or running
uint32_t net_take_events();                // returns and clears pending event bits
void     net_status(char* buf, size_t n);  // latest human-readable status
bool     net_wifi_connected();
int      net_wifi_rssi();
void     net_wifi_ip(char* buf, size_t n);
const LocalInfo& net_local_info();
