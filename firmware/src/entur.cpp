#include "entur.h"
#include "net_util.h"
#include "i18n.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char* GQL_URL  = "https://api.entur.io/journey-planner/v3/graphql";
static const char* CLIENT_HDR = "ET-Client-Name: frogswiper-esp32busboard";

static SemaphoreHandle_t g_mtx;
static DepState* g_state;     // PSRAM
static DepState* g_work;      // PSRAM, filled by the fetch then swapped in
static Journey*  g_journey;
static Tracker*  g_track;
static StopHits  g_hits;
static PsramBuffer g_buf;

void entur_init() {
    g_mtx = xSemaphoreCreateMutex();
    g_state   = (DepState*)ps_calloc(1, sizeof(DepState));
    g_work    = (DepState*)ps_calloc(1, sizeof(DepState));
    g_journey = (Journey*)ps_calloc(1, sizeof(Journey));
    g_track   = (Tracker*)ps_calloc(1, sizeof(Tracker));
}
void entur_lock()   { xSemaphoreTake(g_mtx, portMAX_DELAY); }
void entur_unlock() { xSemaphoreGive(g_mtx); }
DepState& entur_state()   { return *g_state; }
Journey&  entur_journey() { return *g_journey; }
StopHits& entur_hits()    { return g_hits; }
Tracker&  entur_tracker() { return *g_track; }
void entur_clear() {
    entur_lock();
    g_state->count = 0; g_state->sit_count = 0; g_state->have_data = false; g_state->stop_n = 0;
    entur_unlock();
}

// ── time ─────────────────────────────────────────────────────────────────────
static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}
// "2026-10-07T10:45:56+02:00" (also "Z" and fractional seconds) → UTC epoch
time_t entur_parse_time(const char* s) {
    if (!s || strlen(s) < 19) return 0;
    int Y, M, D, h, m, sec;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) != 6) return 0;
    const char* p = s + 19;
    while (*p == '.' || isdigit((unsigned char)*p)) p++;
    int off = 0;
    if (*p == '+' || *p == '-') {
        int oh = 0, om = 0; sscanf(p + 1, "%d:%d", &oh, &om);
        off = (oh * 3600 + om * 60) * (*p == '-' ? -1 : 1);
    }
    return (time_t)(days_from_civil(Y, M, D) * 86400LL + h * 3600 + m * 60 + sec - off);
}

static uint8_t parse_mode(const char* m) {
    if (!m) return TM_OTHER;
    if (!strcmp(m, "bus"))   return TM_BUS;
    if (!strcmp(m, "tram"))  return TM_TRAM;
    if (!strcmp(m, "metro")) return TM_METRO;
    if (!strcmp(m, "rail"))  return TM_RAIL;
    if (!strcmp(m, "water")) return TM_WATER;
    if (!strcmp(m, "coach")) return TM_COACH;
    if (!strcmp(m, "air"))   return TM_AIR;
    return TM_OTHER;
}
const char* entur_mode_name(uint8_t mode) {
    static const char* no[] = {"Buss", "Trikk", "T-bane", "Tog", "Båt", "Ekspressbuss", "Fly", "Annet"};
    static const char* en[] = {"Bus", "Tram", "Metro", "Train", "Boat", "Coach", "Air", "Other"};
    if (mode > TM_OTHER) mode = TM_OTHER;
    return i18n_lang() == LANG_EN ? en[mode] : no[mode];
}
uint32_t entur_mode_color(uint8_t mode) {
    switch (mode) {
        case TM_BUS:   return 0xE60000;
        case TM_TRAM:  return 0x0B91EF;
        case TM_METRO: return 0xEC700C;
        case TM_RAIL:  return 0x00367F;
        case TM_WATER: return 0x0094A8;
        case TM_COACH: return 0x75A300;
        case TM_AIR:   return 0x9D4D86;
        default:       return 0x5A5F8A;
    }
}
static uint32_t parse_hex(const char* s, bool& ok) {
    ok = false;
    if (!s || strlen(s) != 6) return 0;
    char* end; uint32_t v = strtoul(s, &end, 16);
    ok = (*end == 0);
    return v;
}

// ── departures ───────────────────────────────────────────────────────────────
static const char* GQL_FRAGMENT =
    "fragment f on StopPlace { id name latitude longitude "
    "estimatedCalls(numberOfDepartures: 30, timeRange: 86400, arrivalDeparture: departures, includeCancelledTrips: true) { "
    "realtime aimedDepartureTime expectedDepartureTime cancellation date quay { publicCode } "
    "destinationDisplay { frontText } "
    "serviceJourney { id line { publicCode transportMode presentation { colour textColour } } } "
    "situations { summary { value language } } } }";

static void pick_text(JsonArrayConst arr, char* out, size_t n) {
    // prefer the UI language, then Norwegian, then anything
    const char* want = i18n_lang() == LANG_EN ? "en" : "no";
    const char* best = nullptr;
    for (JsonObjectConst t : arr) { const char* l = t["language"] | ""; if (!strcmp(l, want)) { best = t["value"]; break; } }
    if (!best) for (JsonObjectConst t : arr) { const char* l = t["language"] | ""; if (!strcmp(l, "no") || !strcmp(l, "nob") || !l[0]) { best = t["value"]; break; } }
    if (!best && arr.size()) best = arr[0]["value"];
    strlcpy(out, best ? best : "", n);
}

static int cmp_dep(const void* a, const void* b) {
    const Departure* x = (const Departure*)a; const Departure* y = (const Departure*)b;
    return x->expected < y->expected ? -1 : x->expected > y->expected ? 1 : 0;
}

bool entur_fetch_departures() {
    Settings& s = settings_get();
    DepState* w = g_work;
    // snapshot the stop list (the UI may edit it while we fetch)
    int n = min((int)s.stop_count, MAX_STOPS);
    uint32_t ver = settings_stops_version();
    char ids[MAX_STOPS][28];
    for (int i = 0; i < n; i++) strlcpy(ids[i], s.stops[i].id, sizeof(ids[i]));
    if (n == 0) return false;

    static char body[2048];
    size_t o = snprintf(body, sizeof(body), "{\"query\":\"%s {", GQL_FRAGMENT);
    for (int i = 0; i < n; i++) {
        if (!settings_valid_stop_id(ids[i])) continue;
        o += snprintf(body + o, sizeof(body) - o, " s%d: stopPlace(id: \\\"%s\\\") { ...f }", i, ids[i]);
    }
    o += snprintf(body + o, sizeof(body) - o, " }\"}");

    const char* headers[] = {CLIENT_HDR, "Accept: application/json", nullptr};
    uint32_t t0 = millis();
    int rc = http_post_to_buffer(GQL_URL, body, "application/json", headers, g_buf, 15000);
    uint32_t took = millis() - t0;

    auto fail = [&](const char* msg) {
        entur_lock();
        g_state->ok = false; g_state->http = rc; g_state->fail_count++;
        strlcpy(g_state->error, msg, sizeof(g_state->error));
        entur_unlock();
        Serial.printf("[ENTUR] departures failed: %s (rc=%d)\n", msg, rc);
        return false;
    };
    if (rc != 200) { char m[48]; snprintf(m, sizeof(m), "HTTP %d", rc); return fail(m); }

    JsonDocument doc(&g_psram_alloc);
    DeserializationError err = deserializeJson(doc, g_buf.data(), g_buf.size());
    if (err) return fail("bad JSON");
    if (doc["errors"].is<JsonArray>() && !doc["data"].is<JsonObject>()) return fail(doc["errors"][0]["message"] | "GraphQL error");

    memset(w, 0, offsetof(DepState, ok));
    w->stop_n = n; w->stops_ver = ver;
    for (int i = 0; i < n; i++) strlcpy(w->stop_ids[i], ids[i], sizeof(w->stop_ids[i]));
    char key[4];
    for (int i = 0; i < n; i++) {
        snprintf(key, sizeof(key), "s%d", i);
        JsonObjectConst sp = doc["data"][key];
        if (sp.isNull()) continue;
        strlcpy(w->stop_names[i], sp["name"] | "", sizeof(w->stop_names[i]));
        for (JsonObjectConst c : sp["estimatedCalls"].as<JsonArrayConst>()) {
            if (w->count >= MAX_DEPS) break;
            Departure& d = w->deps[w->count];
            memset(&d, 0, sizeof(d));
            d.stop = i;
            d.realtime  = c["realtime"] | false;
            d.cancelled = c["cancellation"] | false;
            d.aimed     = entur_parse_time(c["aimedDepartureTime"] | "");
            d.expected  = entur_parse_time(c["expectedDepartureTime"] | "");
            if (!d.expected) d.expected = d.aimed;
            if (!d.aimed) continue;
            strlcpy(d.date, c["date"] | "", sizeof(d.date));
            strlcpy(d.quay, c["quay"]["publicCode"] | "", sizeof(d.quay));
            strlcpy(d.dest, c["destinationDisplay"]["frontText"] | "", sizeof(d.dest));
            JsonObjectConst sj = c["serviceJourney"];
            strlcpy(d.sj, sj["id"] | "", sizeof(d.sj));
            JsonObjectConst ln = sj["line"];
            strlcpy(d.line, ln["publicCode"] | "", sizeof(d.line));
            d.mode = parse_mode(ln["transportMode"] | "");
            bool ok1, ok2;
            d.color = parse_hex(ln["presentation"]["colour"] | "", ok1);
            d.text_color = parse_hex(ln["presentation"]["textColour"] | "", ok2);
            d.has_color = ok1;
            if (!ok2) d.text_color = 0xFFFFFF;
            d.situation = -1;
            for (JsonObjectConst si : c["situations"].as<JsonArrayConst>()) {
                char txt[SITUATION_LEN];
                pick_text(si["summary"], txt, sizeof(txt));
                if (!txt[0]) continue;
                int found = -1;
                for (int k = 0; k < w->sit_count; k++) if (!strcmp(w->situations[k], txt)) { found = k; break; }
                if (found < 0 && w->sit_count < MAX_SITUATIONS) { found = w->sit_count++; strlcpy(w->situations[found], txt, SITUATION_LEN); }
                if (found >= 0 && d.situation < 0) d.situation = found;
            }
            w->count++;
        }
    }
    qsort(w->deps, w->count, sizeof(Departure), cmp_dep);

    entur_lock();
    DepState* old = g_state;
    w->ok = true; w->have_data = true; w->http = rc; w->error[0] = 0;
    w->fetched_ms = millis(); w->fetched_epoch = time(nullptr); w->took_ms = took; w->bytes = g_buf.size();
    w->fetch_count = old->fetch_count + 1; w->fail_count = old->fail_count;
    g_state = w; g_work = old;
    entur_unlock();
    Serial.printf("[ENTUR] %d departures from %d stops, %u bytes, %u ms\n", w->count, n, (unsigned)g_buf.size(), (unsigned)took);
    return true;
}

// ── journey (all calls of one trip) ──────────────────────────────────────────
static bool fetch_journey_into(Journey* j, const char* sj, const char* date, const char* stop_id, time_t aimed);
bool entur_fetch_journey(const char* sj, const char* date, const char* stop_id, time_t aimed) {
    return fetch_journey_into(g_journey, sj, date, stop_id, aimed);
}
static bool fetch_journey_into(Journey* j, const char* sj, const char* date, const char* stop_id, time_t aimed) {
    for (const char* p = sj; *p; p++) if (*p == '"' || *p == '\\') return false;
    static char body[640];
    snprintf(body, sizeof(body),
        "{\"query\":\"{ serviceJourney(id: \\\"%s\\\") { line { publicCode transportMode presentation { colour textColour } } "
        "estimatedCalls(date: \\\"%s\\\") { quay { publicCode name stopPlace { id } } destinationDisplay { frontText } "
        "aimedDepartureTime expectedDepartureTime aimedArrivalTime expectedArrivalTime actualDepartureTime actualArrivalTime cancellation } } }\"}",
        sj, date);
    const char* headers[] = {CLIENT_HDR, nullptr};
    int rc = http_post_to_buffer(GQL_URL, body, "application/json", headers, g_buf, 15000);
    if (rc != 200) {
        entur_lock(); j->loading = false; entur_unlock();
        Serial.printf("[ENTUR] journey HTTP %d\n", rc);
        return false;
    }
    JsonDocument doc(&g_psram_alloc);
    if (deserializeJson(doc, g_buf.data(), g_buf.size()) || doc["data"]["serviceJourney"].isNull()) {
        entur_lock(); j->loading = false; entur_unlock();
        return false;
    }
    JsonObjectConst sjo = doc["data"]["serviceJourney"];
    entur_lock();
    if (j == g_journey && strcmp(j->sj, sj)) { entur_unlock(); return false; }   // user opened another trip meanwhile
    strlcpy(j->sj, sj, sizeof(j->sj));
    JsonObjectConst ln = sjo["line"];
    strlcpy(j->line, ln["publicCode"] | "", sizeof(j->line));
    j->mode = parse_mode(ln["transportMode"] | "");
    bool ok1, ok2;
    j->color = parse_hex(ln["presentation"]["colour"] | "", ok1);
    j->text_color = parse_hex(ln["presentation"]["textColour"] | "", ok2);
    j->has_color = ok1; if (!ok2) j->text_color = 0xFFFFFF;
    j->count = 0; j->here = -1; j->vehicle = -1; j->dest[0] = 0;
    int best_here = -1; long best_diff = 1L << 30;
    for (JsonObjectConst c : sjo["estimatedCalls"].as<JsonArrayConst>()) {
        if (j->count >= MAX_CALLS) break;
        JourneyCall& k = j->calls[j->count];
        strlcpy(k.name, c["quay"]["name"] | "", sizeof(k.name));
        strlcpy(k.quay, c["quay"]["publicCode"] | "", sizeof(k.quay));
        strlcpy(k.stop_id, c["quay"]["stopPlace"]["id"] | "", sizeof(k.stop_id));
        const char* ad = c["aimedDepartureTime"] | (const char*)nullptr;
        if (!ad) ad = c["aimedArrivalTime"] | "";
        const char* ed = c["expectedDepartureTime"] | (const char*)nullptr;
        if (!ed) ed = c["expectedArrivalTime"] | "";
        k.aimed = entur_parse_time(ad);
        k.expected = entur_parse_time(ed); if (!k.expected) k.expected = k.aimed;
        k.departed = !c["actualDepartureTime"].isNull();
        k.passed = k.departed || !c["actualArrivalTime"].isNull();
        k.cancelled = c["cancellation"] | false;
        if (k.passed) j->vehicle = j->count;
        if (!j->dest[0]) strlcpy(j->dest, c["destinationDisplay"]["frontText"] | "", sizeof(j->dest));
        if (stop_id && !strcmp(k.stop_id, stop_id)) {     // loop routes visit a stop twice: match the time too
            long diff = labs((long)(k.aimed - aimed));
            if (diff < best_diff) { best_diff = diff; best_here = j->count; }
        }
        j->count++;
    }
    j->here = best_here;
    // operators without actual times (or before the first report): estimate from the timetable
    j->estimated = false;
    if (j->vehicle < 0) {
        time_t now = time(nullptr);
        for (int i = 0; i < j->count; i++) if (j->calls[i].expected <= now) { j->vehicle = i; j->calls[i].departed = true; }
        j->estimated = j->vehicle >= 0;
    }
    j->valid = true; j->loading = false; j->fetched_ms = millis();
    entur_unlock();
    return true;
}

// ── stop search ──────────────────────────────────────────────────────────────
static uint8_t category_modes(JsonArrayConst cats) {
    uint8_t m = 0;
    for (const char* c : cats) {
        if (!c) continue;
        if (strstr(c, "Bus") || strstr(c, "bus")) m |= 1 << TM_BUS;
        if (strstr(c, "Tram") || strstr(c, "tram")) m |= 1 << TM_TRAM;
        if (strstr(c, "metro") || strstr(c, "Metro")) m |= 1 << TM_METRO;
        if (strstr(c, "rail") || strstr(c, "Rail")) m |= 1 << TM_RAIL;
        if (strstr(c, "ferry") || strstr(c, "harbour") || strstr(c, "Ferry")) m |= 1 << TM_WATER;
        if (strstr(c, "coach") || strstr(c, "Coach")) m |= 1 << TM_COACH;
        if (strstr(c, "airport")) m |= 1 << TM_AIR;
    }
    return m;
}
static float dist_km(float la1, float lo1, float la2, float lo2) {
    const float R = 6371.0f, d2r = 0.01745329f;
    float dla = (la2 - la1) * d2r, dlo = (lo2 - lo1) * d2r;
    float a = sinf(dla / 2) * sinf(dla / 2) + cosf(la1 * d2r) * cosf(la2 * d2r) * sinf(dlo / 2) * sinf(dlo / 2);
    return 2 * R * atan2f(sqrtf(a), sqrtf(1 - a));
}
static bool parse_hits(bool nearby, float lat, float lon) {
    JsonDocument doc(&g_psram_alloc);
    StopHits tmp = {};
    tmp.nearby = nearby;
    if (deserializeJson(doc, g_buf.data(), g_buf.size())) return false;
    for (JsonObjectConst f : doc["features"].as<JsonArrayConst>()) {
        if (tmp.count >= MAX_HITS) break;
        JsonObjectConst p = f["properties"];
        const char* id = p["id"] | "";
        if (!settings_valid_stop_id(id)) continue;
        bool dup = false;
        for (int i = 0; i < tmp.count; i++) if (!strcmp(tmp.hits[i].id, id)) dup = true;
        if (dup) continue;
        StopHit& h = tmp.hits[tmp.count];
        strlcpy(h.id, id, sizeof(h.id));
        strlcpy(h.name, p["name"] | "", sizeof(h.name));
        strlcpy(h.locality, p["locality"] | (p["county"] | ""), sizeof(h.locality));
        h.lon = f["geometry"]["coordinates"][0] | 0.0f;
        h.lat = f["geometry"]["coordinates"][1] | 0.0f;
        h.dist_km = (lat || lon) ? dist_km(lat, lon, h.lat, h.lon) : -1;
        h.modes = category_modes(p["category"]);
        tmp.count++;
    }
    tmp.ok = true;
    entur_lock(); g_hits = tmp; entur_unlock();
    return true;
}
bool entur_search(const char* text, float lat, float lon) {
    char enc[128]; url_encode(text, enc, sizeof(enc));
    char url[320];
    if (lat || lon)
        snprintf(url, sizeof(url), "https://api.entur.io/geocoder/v1/autocomplete?text=%s&size=%d&layers=venue&lang=no&focus.point.lat=%.4f&focus.point.lon=%.4f", enc, MAX_HITS, lat, lon);
    else
        snprintf(url, sizeof(url), "https://api.entur.io/geocoder/v1/autocomplete?text=%s&size=%d&layers=venue&lang=no", enc, MAX_HITS);
    const char* headers[] = {CLIENT_HDR, nullptr};
    int rc = http_get_to_buffer(url, g_buf, 10000, headers);
    if (rc != 200) { Serial.printf("[ENTUR] search HTTP %d\n", rc); return false; }
    return parse_hits(false, lat, lon);
}
bool entur_nearby(float lat, float lon) {
    char url[256];
    snprintf(url, sizeof(url), "https://api.entur.io/geocoder/v1/reverse?point.lat=%.5f&point.lon=%.5f&boundary.circle.radius=2&size=%d&layers=venue", lat, lon, MAX_HITS);
    const char* headers[] = {CLIENT_HDR, nullptr};
    int rc = http_get_to_buffer(url, g_buf, 10000, headers);
    if (rc != 200) { Serial.printf("[ENTUR] nearby HTTP %d\n", rc); return false; }
    return parse_hits(true, lat, lon);
}

// ── filters / formatting ─────────────────────────────────────────────────────
static bool csv_has(const char* csv, const char* item, bool& any_positive, bool& excluded) {
    // returns true when `item` is listed positively; sets excluded when listed as "-item"
    any_positive = false; excluded = false;
    bool hit = false;
    const char* p = csv;
    while (*p) {
        while (*p == ',' || *p == ' ') p++;
        if (!*p) break;
        bool neg = *p == '-'; if (neg) p++;
        const char* e = p; while (*e && *e != ',') e++;
        const char* t = e; while (t > p && t[-1] == ' ') t--;
        size_t len = t - p;
        if (!neg) any_positive = true;
        if (len && len == strlen(item) && !strncasecmp(p, item, len)) { if (neg) excluded = true; else hit = true; }
        p = e;
    }
    return hit;
}

bool entur_visible(const Departure& d, time_t now, bool merged, int cur_stop) {
    const Settings& s = settings_get();
    if (!merged && d.stop != cur_stop) return false;
    if (d.expected < now - 20) return false;                 // already gone
    if (d.stop < s.stop_count) {
        const StopCfg& sc = s.stops[d.stop];
        bool pos, ex;
        bool hit = csv_has(sc.lines, d.line, pos, ex);
        if (ex || (pos && !hit)) return false;
        hit = csv_has(sc.quays, d.quay, pos, ex);
        if (ex || (pos && !hit)) return false;
        if (s.hide_unreachable && sc.walk_min && d.expected - now < sc.walk_min * 60) return false;
    }
    return true;
}

int entur_minutes(const Departure& d, time_t now) {
    long sec = (long)(d.expected - now);
    return sec >= 0 ? (int)(sec / 60) : -(int)((-sec + 59) / 60);
}

void entur_format_time(const Departure& d, time_t now, char* buf, size_t n) {
    const Settings& s = settings_get();
    int m = entur_minutes(d, now);
    // Ruter convention: minutes only for realtime departures in the near future, clock time otherwise
    if (d.realtime && (s.clock_after == 0 || m < s.clock_after)) {
        if (m <= 0) strlcpy(buf, TR("nå", "now"), n);
        else snprintf(buf, n, "%d min", m);
        return;
    }
    struct tm tm; time_t t = d.expected; localtime_r(&t, &tm);
    snprintf(buf, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

bool entur_leave_now(const Departure& d, time_t now) {
    const Settings& s = settings_get();
    if (d.cancelled || d.stop >= s.stop_count) return false;
    int walk = s.stops[d.stop].walk_min;
    if (!walk) return false;
    long sec = (long)(d.expected - now);
    return sec >= walk * 60 && sec < (walk + 2) * 60;
}

// ── line tracking ────────────────────────────────────────────────────────────
bool entur_track_configured() {
    const Settings& s = settings_get();
    if (!s.track_line[0] || !s.track_stop[0]) return false;
    for (int i = 0; i < s.stop_count; i++) if (!strcmp(s.stops[i].id, s.track_stop)) return true;
    return false;
}
bool entur_track_matches(const Departure& d, int stop_idx) {
    const Settings& s = settings_get();
    if (!s.track_line[0] || strcasecmp(d.line, s.track_line)) return false;
    if (s.track_dest[0] && strcmp(d.dest, s.track_dest)) return false;
    const DepState& st = *g_state;
    return stop_idx < st.stop_n && !strcmp(st.stop_ids[stop_idx], s.track_stop);
}
int entur_track_select(time_t now) {
    Tracker& t = *g_track;
    t.req_n = 0;
    if (!entur_track_configured()) { t.count = 0; return 0; }
    const DepState& st = *g_state;
    strlcpy(t.stop_id, settings_get().track_stop, sizeof(t.stop_id));
    for (int i = 0; i < st.count && t.req_n < TRACK_TRIPS; i++) {
        const Departure& d = st.deps[i];
        if (d.cancelled || d.expected < now - 30 || !entur_track_matches(d, d.stop)) continue;
        TrackReq& r = t.req[t.req_n++];
        strlcpy(r.sj, d.sj, sizeof(r.sj)); strlcpy(r.date, d.date, sizeof(r.date)); r.aimed = d.aimed;
    }
    return t.req_n;
}
bool entur_fetch_track() {
    entur_lock();
    Tracker& t = *g_track;
    int n = t.req_n; TrackReq req[TRACK_TRIPS]; memcpy(req, t.req, sizeof(req));
    char stop[28]; strlcpy(stop, t.stop_id, sizeof(stop));
    entur_unlock();
    int ok = 0;
    for (int i = 0; i < n; i++)
        if (fetch_journey_into(&t.trips[ok], req[i].sj, req[i].date, stop, req[i].aimed) && t.trips[ok].here >= 0
            && entur_track_stops_away(t.trips[ok]) >= 0) ok++;    // skip a trip that already left your stop
    entur_lock(); t.count = ok; t.fetched_ms = millis(); entur_unlock();
    Serial.printf("[ENTUR] tracking %s: %d trips\n", settings_get().track_line, ok);
    return ok > 0;
}
int entur_track_stops_away(const Journey& j) {
    if (j.here < 0) return -1;
    if (j.vehicle == j.here && !j.calls[j.here].departed) return 0;   // standing at your stop
    if (j.vehicle >= j.here) return -1;
    return j.here - j.vehicle - 1;          // 0 = your stop is the next one
}
