#include "ui_settings.h"
#include "ui_main.h"
#include "ui_board.h"
#include "theme.h"
#include "../board_config.h"
#include "../settings.h"
#include "../net_task.h"
#include "../display.h"
#include "../i18n.h"
#include <Arduino.h>

static lv_obj_t *g_form, *g_status, *g_loc;
static lv_obj_t *g_ta_ssid, *g_ta_pass, *g_ta_lat, *g_ta_lon, *g_ta_alines, *g_ta_ntfy;
static lv_obj_t *g_dd_mode, *g_dd_rot, *g_dd_upd, *g_dd_clk, *g_dd_theme, *g_dd_lang, *g_dd_loc;
static lv_obj_t *g_sw_unreach, *g_sw_plat, *g_sw_lcol, *g_sw_tick, *g_sw_wx, *g_sw_night;
static lv_obj_t *g_sl_bright;

static const uint8_t  ROT_STEPS[] = {0, 10, 15, 20, 30, 60};
static const uint16_t UPD_STEPS[] = {15, 20, 30, 45, 60, 120};
static const uint8_t  CLK_STEPS[] = {10, 15, 20, 30, 60, 0};
template <typename T, size_t N> static int step_index(const T (&arr)[N], T v) { for (size_t i = 0; i < N; i++) if (arr[i] == v) return i; return 0; }

void ui_settings_set_status(const char* m) { if (g_status) lv_label_set_text(g_status, m); }

void ui_settings_location_changed() {
    const Settings& s = settings_get();
    float la, lo; settings_weather_position(la, lo);
    char t[120];
    const char* src = s.loc_mode == LOC_MANUAL ? TR("manuell", "manual") : s.stop_count ? TR("første stopp", "first stop") : "IP";
    snprintf(t, sizeof(t), TR("Værposisjon: %.3f, %.3f (%s)", "Weather position: %.3f, %.3f (%s)"), la, lo, src);
    lv_label_set_text(g_loc, t);
}

// ── widgets ──────────────────────────────────────────────────────────────────
static lv_obj_t* header(const char* text) {
    lv_obj_t* l = ui_label(g_form, &font_b16, theme().text, text);
    lv_obj_set_style_pad_top(l, 10, 0);
    return l;
}
static lv_obj_t* row(const char* text) {
    lv_obj_t* r = ui_plain(g_form);
    lv_obj_set_size(r, LV_PCT(100), 40);
    lv_obj_t* l = ui_label(r, &font_m14, theme().text, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 120);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    return r;
}
static lv_event_cb_t g_opts_cb;
static lv_obj_t* dropdown(const char* label, const char* opts, int sel, lv_coord_t w = 130) {
    lv_obj_t* r = row(label);
    lv_obj_t* dd = lv_dropdown_create(r);
    lv_dropdown_set_options(dd, opts);
    lv_dropdown_set_selected(dd, sel);
    lv_obj_set_width(dd, w);
    lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
    ui_style_dropdown(dd);
    lv_obj_add_event_cb(dd, g_opts_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return dd;
}
static lv_obj_t* toggle(const char* label, bool on) {
    lv_obj_t* r = row(label);
    lv_obj_t* l = lv_obj_get_child(r, 0);
    lv_obj_set_width(l, 180);
    lv_obj_t* sw = lv_switch_create(r);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    ui_style_switch(sw);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, g_opts_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return sw;
}
static void ta_cb(lv_event_t* e) {
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
    ui_kb_attach(ta, ta == g_ta_lat || ta == g_ta_lon, g_form);
}
static lv_obj_t* field(const char* ph, const char* val, bool pw = false) {
    lv_obj_t* ta = ui_textarea(g_form, ph, val, pw);
    lv_obj_add_event_cb(ta, ta_cb, LV_EVENT_FOCUSED, nullptr);
    return ta;
}
static lv_obj_t* wide_button(const char* text, lv_event_cb_t cb) {
    lv_obj_t* b = ui_button(g_form, text, cb);
    lv_obj_set_width(b, LV_PCT(100));
    return b;
}

// ── callbacks ────────────────────────────────────────────────────────────────
static void save_wifi_cb(lv_event_t*) {
    Settings& s = settings_get();
    strlcpy(s.wifi_ssid,     lv_textarea_get_text(g_ta_ssid), sizeof(s.wifi_ssid));
    strlcpy(s.wifi_password, lv_textarea_get_text(g_ta_pass), sizeof(s.wifi_password));
    settings_save();
    ui_kb_hide();
    ui_settings_set_status(TR("Kobler til...", "Connecting..."));
    net_send(NC_CONNECT_WIFI);
}
static void opts_cb(lv_event_t* e) {
    Settings& s = settings_get();
    lv_obj_t* t = (lv_obj_t*)lv_event_get_target(e);
    bool restart = false;
    if (t == g_dd_theme && lv_dropdown_get_selected(g_dd_theme) != s.theme) { s.theme = lv_dropdown_get_selected(g_dd_theme); restart = true; }
    if (t == g_dd_lang  && lv_dropdown_get_selected(g_dd_lang)  != s.lang)  { s.lang  = lv_dropdown_get_selected(g_dd_lang);  restart = true; }
    s.board_mode  = lv_dropdown_get_selected(g_dd_mode);
    s.rotate_s    = ROT_STEPS[lv_dropdown_get_selected(g_dd_rot)];
    s.update_s    = UPD_STEPS[lv_dropdown_get_selected(g_dd_upd)];
    s.clock_after = CLK_STEPS[lv_dropdown_get_selected(g_dd_clk)];
    s.hide_unreachable = lv_obj_has_state(g_sw_unreach, LV_STATE_CHECKED);
    s.show_platform    = lv_obj_has_state(g_sw_plat,    LV_STATE_CHECKED);
    s.line_colours     = lv_obj_has_state(g_sw_lcol,    LV_STATE_CHECKED);
    s.ticker           = lv_obj_has_state(g_sw_tick,    LV_STATE_CHECKED);
    s.show_weather     = lv_obj_has_state(g_sw_wx,      LV_STATE_CHECKED);
    s.night_dim        = lv_obj_has_state(g_sw_night,   LV_STATE_CHECKED);
    s.brightness       = lv_slider_get_value(g_sl_bright);
    settings_save();
    display_set_brightness(s.brightness);
    if (restart) { ui_settings_set_status(TR("Starter på nytt...", "Restarting...")); lv_refr_now(nullptr); delay(300); ESP.restart(); }
    ui_board_refresh();
}
static void loc_mode_cb(lv_event_t*) {
    Settings& s = settings_get();
    s.loc_mode = lv_dropdown_get_selected(g_dd_loc);
    settings_save();
    ui_settings_location_changed();
    net_send(NC_LOCAL_INFO);
}
static void use_coords_cb(lv_event_t*) {
    Settings& s = settings_get();
    float la = atof(lv_textarea_get_text(g_ta_lat)), lo = atof(lv_textarea_get_text(g_ta_lon));
    if (la < 57 || la > 72 || lo < 4 || lo > 32) { ui_settings_set_status(TR("Ugyldige koordinater (Norge)", "Invalid coordinates (Norway)")); return; }
    s.man_lat = la; s.man_lon = lo; s.loc_mode = LOC_MANUAL;
    lv_dropdown_set_selected(g_dd_loc, LOC_MANUAL);
    settings_save(); ui_kb_hide();
    ui_settings_location_changed();
    net_send(NC_LOCAL_INFO);
}
static void locate_cb(lv_event_t*) { ui_settings_set_status(TR("Finner posisjon...", "Locating...")); net_send(NC_LOCATE_IP); }
static void save_alerts_cb(lv_event_t*) {
    Settings& s = settings_get();
    strlcpy(s.alert_lines, lv_textarea_get_text(g_ta_alines), sizeof(s.alert_lines));
    strlcpy(s.ntfy_topic,  lv_textarea_get_text(g_ta_ntfy),   sizeof(s.ntfy_topic));
    settings_save(); ui_kb_hide();
    ui_settings_set_status(TR("Varsler lagret", "Alerts saved"));
}

// ── build ────────────────────────────────────────────────────────────────────
void ui_settings_build(lv_obj_t* parent) {
    const Theme& th = theme();
    const Settings& s = settings_get();
    g_opts_cb = opts_cb;
    g_form = lv_obj_create(parent);
    lv_obj_set_size(g_form, DISPLAY_WIDTH, CONTENT_HEIGHT);
    lv_obj_set_style_bg_color(g_form, tc(th.bg), 0);
    lv_obj_set_style_border_width(g_form, 0, 0);
    lv_obj_set_style_radius(g_form, 0, 0);
    lv_obj_set_style_pad_all(g_form, 12, 0);
    lv_obj_set_style_pad_row(g_form, 6, 0);
    lv_obj_set_flex_flow(g_form, LV_FLEX_FLOW_COLUMN);

    g_status = ui_label(g_form, &font_m12, th.accent, "");
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_status, LV_PCT(100));

    header("WiFi");
    g_ta_ssid = field(TR("Nettverksnavn (SSID)", "Network name (SSID)"), s.wifi_ssid);
    g_ta_pass = field(TR("Passord", "Password"), s.wifi_password, true);
    wide_button(TR("Lagre og koble til", "Save & connect"), save_wifi_cb);

    header(TR("Tavle", "Board"));
    g_dd_mode = dropdown(TR("Visning", "View"), TR("Ett stopp\nAlle samlet", "One stop\nAll merged"), s.board_mode);
    g_dd_rot  = dropdown(TR("Bytt stopp", "Cycle stops"), TR("Av\n10 s\n15 s\n20 s\n30 s\n60 s", "Off\n10 s\n15 s\n20 s\n30 s\n60 s"), step_index(ROT_STEPS, s.rotate_s));
    g_dd_upd  = dropdown(TR("Oppdater", "Refresh"), "15 s\n20 s\n30 s\n45 s\n60 s\n120 s", step_index(UPD_STEPS, s.update_s));
    g_dd_clk  = dropdown(TR("Klokkeslett etter", "Clock time after"), TR("10 min\n15 min\n20 min\n30 min\n60 min\nAldri", "10 min\n15 min\n20 min\n30 min\n60 min\nNever"), step_index(CLK_STEPS, s.clock_after));
    g_sw_unreach = toggle(TR("Skjul avganger du ikke rekker", "Hide departures you can't reach"), s.hide_unreachable);
    g_sw_plat = toggle(TR("Vis plattform / spor", "Show platform / track"), s.show_platform);
    g_sw_lcol = toggle(TR("Linjefarger", "Line colours"), s.line_colours);
    g_sw_tick = toggle(TR("Avviksmeldinger nederst", "Disruption ticker"), s.ticker);
    g_sw_wx   = toggle(TR("Vær i toppen", "Weather in header"), s.show_weather);

    header(TR("Skjerm", "Display"));
    static char theme_opts[160]; theme_opts[0] = 0;
    for (int i = 0; i < THEME_COUNT; i++) { if (i) strlcat(theme_opts, "\n", sizeof(theme_opts)); strlcat(theme_opts, THEMES[i].name, sizeof(theme_opts)); }
    g_dd_theme = dropdown(TR("Tema", "Theme"), theme_opts, s.theme < THEME_COUNT ? s.theme : 0);
    g_dd_lang  = dropdown(TR("Språk", "Language"), "Norsk\nEnglish", s.lang);
    lv_obj_t* br = row(TR("Lysstyrke", "Brightness"));
    g_sl_bright = lv_slider_create(br);
    lv_slider_set_range(g_sl_bright, 10, 255);
    lv_slider_set_value(g_sl_bright, s.brightness, LV_ANIM_OFF);
    lv_obj_set_width(g_sl_bright, 120);
    lv_obj_align(g_sl_bright, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_bg_color(g_sl_bright, tc(th.line), 0);
    lv_obj_set_style_bg_color(g_sl_bright, tc(th.accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_sl_bright, tc(th.text), LV_PART_KNOB);
    lv_obj_add_event_cb(g_sl_bright, opts_cb, LV_EVENT_RELEASED, nullptr);
    char nd[64]; snprintf(nd, sizeof(nd), TR("Dempet om natten (%02d-%02d)", "Dim at night (%02d-%02d)"), s.night_from, s.night_to);
    g_sw_night = toggle(nd, s.night_dim);

    header(TR("Værposisjon", "Weather position"));
    g_loc = ui_label(g_form, &font_m12, th.dim, "");
    lv_label_set_long_mode(g_loc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_loc, LV_PCT(100));
    g_dd_loc = dropdown(TR("Kilde", "Source"), TR("Første stopp\nManuell", "First stop\nManual"), s.loc_mode, 140);
    lv_obj_remove_event_cb(g_dd_loc, opts_cb);
    lv_obj_add_event_cb(g_dd_loc, loc_mode_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    char la[16] = "", lo[16] = "";
    if (s.man_lat || s.man_lon) { snprintf(la, sizeof(la), "%.4f", s.man_lat); snprintf(lo, sizeof(lo), "%.4f", s.man_lon); }
    g_ta_lat = field(TR("Breddegrad, f.eks. 60.1680", "Latitude, e.g. 60.1680"), la);
    g_ta_lon = field(TR("Lengdegrad, f.eks. 10.2560", "Longitude, e.g. 10.2560"), lo);
    wide_button(TR("Bruk koordinater", "Use coordinates"), use_coords_cb);
    wide_button(TR("Finn via offentlig IP", "Locate by public IP"), locate_cb);

    header(TR("\"Gå nå\"-varsel", "\"Leave now\" alerts"));
    lv_obj_t* hint = ui_label(g_form, &font_m12, th.dim, TR("Push til ntfy.sh når det er gangtid + 2 min til avgang for disse linjene.", "Push to ntfy.sh when a departure of these lines is walk time + 2 min away."));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));
    g_ta_alines = field(TR("Linjer, f.eks. 223,F4", "Lines, e.g. 223,F4"), s.alert_lines);
    g_ta_ntfy   = field(TR("ntfy.sh-emne", "ntfy.sh topic"), s.ntfy_topic);
    wide_button(TR("Lagre varsler", "Save alerts"), save_alerts_cb);

    lv_obj_t* pad = ui_plain(g_form);
    lv_obj_set_size(pad, 10, 20);
    ui_settings_location_changed();
}

void ui_settings_debug(char c) {
    if (c == 'K') { lv_obj_scroll_to_y(g_form, 0, LV_ANIM_OFF); ui_kb_attach(g_ta_ssid, false, g_form); }
    if (c == 'O') lv_dropdown_open(g_dd_theme);
}
