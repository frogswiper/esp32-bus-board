#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "settings.h"

// Entur Journey Planner v3 (GraphQL) + Geocoder — open Norwegian public-transport data, no API key.
// All fetches run on the network task; the UI reads the shared state under entur_lock().

enum TransMode : uint8_t { TM_BUS, TM_TRAM, TM_METRO, TM_RAIL, TM_WATER, TM_COACH, TM_AIR, TM_OTHER };

struct Departure {
    uint8_t  stop;              // index into the stop snapshot of this fetch
    uint8_t  mode;              // TransMode
    bool     realtime, cancelled, has_color;
    uint32_t color, text_color;
    int8_t   situation;         // index into DepState::situations, -1 = none
    time_t   aimed, expected;   // UTC epoch
    char     line[10];
    char     dest[44];
    char     quay[6];
    char     sj[72];            // serviceJourney id (for the journey page)
    char     date[11];          // operating date YYYY-MM-DD
};

#define MAX_DEPS        (MAX_STOPS * 30)   // the query asks 30 calls per stop
#define MAX_SITUATIONS  8
#define SITUATION_LEN   200

struct DepState {
    Departure deps[MAX_DEPS];
    int       count;
    char      situations[MAX_SITUATIONS][SITUATION_LEN];
    int       sit_count;
    char      stop_ids[MAX_STOPS][28];     // snapshot the departures refer to
    char      stop_names[MAX_STOPS][40];
    int       stop_n;
    uint32_t  stops_ver;
    // fetch status
    bool      ok;                // last fetch succeeded
    bool      have_data;
    int       http;
    char      error[64];
    uint32_t  fetched_ms;        // millis() of last success
    time_t    fetched_epoch;
    uint32_t  took_ms;
    uint32_t  fetch_count, fail_count;
    size_t    bytes;
};

struct JourneyCall {
    char   name[40];
    char   stop_id[28];
    char   quay[6];
    time_t aimed, expected;
    bool   passed;               // actual departure/arrival recorded
    bool   departed;             // actual departure recorded (arrived-only = standing at the stop)
    bool   cancelled;
};
#define MAX_CALLS 80
struct Journey {
    bool      valid, loading;
    char      sj[72];
    char      line[10];
    char      dest[44];
    uint8_t   mode;
    bool      has_color;
    uint32_t  color, text_color;
    JourneyCall calls[MAX_CALLS];
    int       count;
    int       here;              // index of the stop the user tapped from (-1)
    int       vehicle;           // index of the last passed call (-1 = not started)
    bool      estimated;         // no actual times reported: vehicle position guessed from the timetable
    uint32_t  fetched_ms;
};

// Line tracking: the next trips of one line (+ optional destination) towards one saved stop.
#define TRACK_TRIPS 2
struct TrackReq { char sj[72]; char date[11]; time_t aimed; };
struct Tracker {
    Journey  trips[TRACK_TRIPS];
    int      count;              // valid trips
    TrackReq req[TRACK_TRIPS];   // what the next fetch should load
    int      req_n;
    char     stop_id[28];
    uint32_t fetched_ms;
};

struct StopHit {
    char    id[28];
    char    name[40];
    char    locality[32];
    float   lat, lon;
    float   dist_km;             // -1 when unknown
    uint8_t modes;               // bit per TransMode
};
#define MAX_HITS 12
struct StopHits {
    StopHit hits[MAX_HITS];
    int     count;
    bool    nearby;              // result of a "near me" lookup
    bool    ok;
};

void        entur_init();
void        entur_lock();
void        entur_unlock();
DepState&   entur_state();       // lock while reading
Journey&    entur_journey();
StopHits&   entur_hits();
Tracker&    entur_tracker();
bool        entur_track_configured();
bool        entur_track_matches(const Departure& d, int stop_idx);   // line/destination/stop match the tracking choice
int         entur_track_select(time_t now);  // pick the next trips from the departures (call under lock); returns count
bool        entur_fetch_track();             // network task: load the selected trips
int         entur_track_stops_away(const Journey& j);                 // stops left before the user's stop (-1 = passed/unknown)
void        entur_clear();

// network-task side (blocking)
bool        entur_fetch_departures();
bool        entur_fetch_journey(const char* sj, const char* date, const char* stop_id, time_t aimed);
bool        entur_search(const char* text, float near_lat, float near_lon);
bool        entur_nearby(float lat, float lon);

// helpers for the UI
bool        entur_visible(const Departure& d, time_t now, bool merged, int cur_stop);   // applies every filter
int         entur_minutes(const Departure& d, time_t now);                             // rounded down, may be negative
void        entur_format_time(const Departure& d, time_t now, char* buf, size_t n);    // "nå" / "5 min" / "10:46"
const char* entur_mode_name(uint8_t mode);
uint32_t    entur_mode_color(uint8_t mode);
time_t      entur_parse_time(const char* iso);
bool        entur_leave_now(const Departure& d, time_t now);                           // inside the walk window
