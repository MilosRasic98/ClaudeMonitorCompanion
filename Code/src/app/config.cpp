#include "config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "secrets.h"
#include "tuning.h"

namespace config {
namespace {

// The defaults come from tuning.h, so the compiled-in behaviour and the
// factory-reset behaviour can never drift apart.
const Field kFields[] = {
    // group      key                   label                    type         min   max     default                hint
    {"Sound",  "piezo_repeats",      "Buzz repeats",           Type::Int,     0,     5, PIEZO_REPEATS,          "2 gives the buzz-buzz", nullptr, nullptr},
    {"Sound",  "piezo_hz1",          "First tone (Hz)",        Type::Int,   500,  6000, PIEZO_BEEP1_HZ,         nullptr, nullptr, nullptr},
    {"Sound",  "piezo_hz2",          "Second tone (Hz)",       Type::Int,   500,  6000, PIEZO_BEEP2_HZ,         "Same as the first for a flat buzz", nullptr, nullptr},
    {"Sound",  "piezo_duty",         "Volume",                 Type::Int,     0,   255, PIEZO_DUTY,             "Loudest near 128", nullptr, nullptr},
    {"Sound",  "piezo_gap_ms",       "Gap inside a buzz (ms)", Type::Int,     0,   500, PIEZO_GAP_MS,           nullptr, nullptr, nullptr},
    {"Sound",  "piezo_rep_gap_ms",   "Gap between buzzes (ms)",Type::Int,     0,  1000, PIEZO_REPEAT_GAP_MS,    "Wider than the inner gap, or it reads as four beeps", nullptr, nullptr},
    {"Sound",  "alert_sound",        "Sound when blocked",     Type::Enum,    0,     3, 3,                      "Needs input, angry, or the usage limit", "none,bell,buzzer,both", nullptr},
    {"Sound",  "error_sound",        "Sound on errors",        Type::Enum,    0,     3, 2,                      nullptr, "none,bell,buzzer,both", nullptr},
    {"Sound",  "done_sound",         "Sound when finished",    Type::Enum,    0,     3, 2,                      "The short double buzz", "none,bell,buzzer,both", nullptr},
    {"Sound",  "piezo_long_ms",      "Long buzz length (ms)",  Type::Int,   100,  3000, PIEZO_LONG_MS,          "The buzzer's bad-news sound", nullptr, nullptr},
    {"Sound",  "bell_pin",           "Bell striker GPIO",      Type::Int,    -1,    48, -1,                     "-1 disables. Free pins: 17, 18, 21, 38, 47, 48", nullptr, nullptr},

    {"Face",   "style",              "Face style",             Type::Enum,    0,     8, 0,                      "Also changed by swiping the screen", "plain,grin,round,pixel,shades,custom1,custom2,custom3,custom4", nullptr},
    {"Face",   "orange",             "Background shade",       Type::Enum,    0,     3, ORANGE_DEFAULT,         "Match it to your printed shell by eye", "saturated,bright,deep,amber", nullptr},
    {"Face",   "eye_w",              "Eye width",              Type::Int,    10,   100, EYE_W,                  nullptr, nullptr, nullptr},
    {"Face",   "eye_h",              "Eye height",             Type::Int,    10,   200, EYE_H,                  nullptr, nullptr, nullptr},
    {"Face",   "eye_gap",            "Gap between eyes",       Type::Int,     0,   160, EYE_GAP,                nullptr, nullptr, nullptr},
    {"Face",   "eye_center_y",       "Eye height on screen",   Type::Int,    40,   240, EYE_CENTER_Y,           "Lower number sits higher up", nullptr, nullptr},
    {"Face",   "grid_q",             "Pixel size",             Type::Int,     1,    20, GRID_Q,                 "Everything snaps to this. Bigger is chunkier and jerkier", nullptr, nullptr},
    {"Face",   "blink_min_ms",       "Fastest blink gap (ms)", Type::Int,   200, 20000, BLINK_MIN_GAP_MS,       nullptr, nullptr, nullptr},
    {"Face",   "swipe_swap_axes",    "Swap swipe axes",        Type::Bool,    0,     1, 0,                      "If sideways swipes change the view instead of the face, turn this on", nullptr, nullptr},
    {"Face",   "blink_max_ms",       "Slowest blink gap (ms)", Type::Int,   200, 30000, BLINK_MAX_GAP_MS,       nullptr, nullptr, nullptr},

    {"Timing", "bored_after_min",    "Bored after (minutes)",  Type::Int,     1,   240, 5,                      "Since your last prompt", nullptr, nullptr},
    {"Timing", "sleep_after_min",    "Asleep after (minutes)", Type::Int,     1,   720, 30,                     "Since anything at all happened", nullptr, nullptr},
    {"Timing", "excited_s",          "Celebration (seconds)",  Type::Int,     1,    60, 6,                      nullptr, nullptr, nullptr},
    {"Timing", "angry_s",            "Sulk length (seconds)",  Type::Int,     1,    60, 7,                      nullptr, nullptr, nullptr},
    {"Timing", "alert_max_s",        "Alert gives up after (s)",Type::Int,   10,  1800, 180,                    "Safety net so it can never stay red forever", nullptr, nullptr},
    {"Timing", "poke_count",         "Pokes before annoyed",   Type::Int,     2,    20, POKE_ANGRY_COUNT,       nullptr, nullptr, nullptr},
    {"Timing", "window_hours",       "Usage window (hours)",   Type::Int,     1,    24, 5,                      "Anthropic's rolling limit window", nullptr, nullptr},
    {"Timing", "poke_window_s",      "Poke window (seconds)",  Type::Int,     1,    30, 4,                      nullptr, nullptr, nullptr},

    {"Screen", "bl_sleeping",        "Brightness asleep",      Type::Int,     0,   255, BACKLIGHT_SLEEPING,     nullptr, nullptr, nullptr},
    {"Screen", "bl_bored",           "Brightness bored",       Type::Int,     0,   255, BACKLIGHT_BORED,        nullptr, nullptr, nullptr},
    {"Screen", "bl_normal",          "Brightness normal",      Type::Int,     0,   255, BACKLIGHT_NORMAL,       nullptr, nullptr, nullptr},
    {"Screen", "bl_excited",         "Brightness alerting",    Type::Int,     0,   255, BACKLIGHT_EXCITED,      nullptr, nullptr, nullptr},

    {"Wi-Fi",  "wifi_ssid",          "Wi-Fi network",          Type::Text,     0,    32, 0,                      "2.4 GHz only - the ESP32 has no 5 GHz radio", nullptr, WIFI_SSID},
    {"Wi-Fi",  "wifi_pass",          "Wi-Fi password",         Type::Password, 0,    63, 0,                      "Leave blank to keep the current one", nullptr, WIFI_PASS},
    {"Wi-Fi",  "mdns_name",          "Hostname",               Type::Text,     0,    24, 0,                      "Reachable as <name>.local on macOS", nullptr, MDNS_NAME},
    {"Wi-Fi",  "tz",                 "Timezone",               Type::Text,     0,    48, 0,                      "POSIX TZ string, e.g. CET-1CEST,M3.5.0,M10.5.0/3", nullptr, "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Wi-Fi",  "token",              "Hook token",             Type::Password, 0,    48, 0,                      "The bearer token your Claude Code hooks send", nullptr, NOTIFY_TOKEN},
};

const size_t kCount = sizeof(kFields) / sizeof(kFields[0]);

int32_t s_values[kCount];
Preferences s_prefs;
bool s_ap_mode = false;

// Strings are held in RAM as well as NVS: the Wi-Fi stack wants a stable char*
// and re-reading Preferences on every reconnect would be silly.
struct StrVal { char v[64]; };
StrVal s_strings[kCount];

int index_of(const char *key) {
  for (size_t i = 0; i < kCount; i++) {
    if (strcmp(kFields[i].key, key) == 0) return (int)i;
  }
  return -1;
}

// NVS keys are capped at 15 characters, and several of ours are longer. Rather
// than shortening the readable names, store by index — the table order is the
// contract, and reset_defaults() covers any reordering.
void nvs_key(size_t i, char *out, size_t n) { snprintf(out, n, "f%u", (unsigned)i); }

}  // namespace

// Values are keyed by table index, so inserting or removing a field shifts
// every later one and each would silently inherit its neighbour's stored value
// — in range, plausible, and wrong. Fingerprint the table and start clean when
// it changes. Losing settings on a firmware update is annoying; silently
// applying the wrong ones is worse.
uint32_t schema_fingerprint() {
  uint32_t h = 2166136261u;  // FNV-1a
  for (size_t i = 0; i < kCount; i++) {
    for (const char *p = kFields[i].key; *p; p++) {
      h = (h ^ (uint8_t)*p) * 16777619u;
    }
    h = (h ^ (uint8_t)kFields[i].type) * 16777619u;
  }
  return h;
}

void begin() {
  s_prefs.begin("mascotcfg", false);

  const uint32_t fp = schema_fingerprint();
  const bool schema_changed = s_prefs.getULong("schema", 0) != fp;


  if (schema_changed) {
    log_w("config: settings table changed, restoring defaults");
    reset_defaults();
    s_prefs.putULong("schema", fp);
  }

  // The JSON emitter groups by consecutive runs, so a group appearing twice in
  // the table would silently render as two sections with the same name.
  for (size_t i = 2; i < kCount; i++) {
    if (strcmp(kFields[i].group, kFields[i - 1].group) == 0) continue;
    for (size_t j = 0; j + 1 < i; j++) {
      if (strcmp(kFields[j].group, kFields[i].group) == 0) {
        log_e("config: group '%s' is not contiguous in the table", kFields[i].group);
      }
    }
  }

  char k[8];
  for (size_t i = 0; i < kCount; i++) {
    nvs_key(i, k, sizeof(k));
    const int32_t stored =
        schema_changed ? kFields[i].def : s_prefs.getInt(k, kFields[i].def);
    // A stored value outside the current range means the table changed under
    // it; fall back rather than honouring something nonsensical.
    s_values[i] = (stored < kFields[i].min || stored > kFields[i].max)
                      ? kFields[i].def
                      : stored;

    if (kFields[i].sdef) {
      char sk[10];
      snprintf(sk, sizeof(sk), "s%u", (unsigned)i);
      String stored_s = schema_changed ? String(kFields[i].sdef)
                                       : s_prefs.getString(sk, kFields[i].sdef);
      strncpy(s_strings[i].v, stored_s.c_str(), sizeof(s_strings[i].v) - 1);
      s_strings[i].v[sizeof(s_strings[i].v) - 1] = '\0';
    }
  }
}

const char *get_str(const char *key) {
  const int i = index_of(key);
  return (i < 0 || !kFields[i].sdef) ? "" : s_strings[i].v;
}

bool set_str(const char *key, const char *value) {
  const int i = index_of(key);
  if (i < 0 || !kFields[i].sdef) return false;
  if (value == nullptr) return false;
  // An empty password means "unchanged", not "erase it" — the page never sends
  // the stored value back, so a blank field is the normal case.
  if (value[0] == '\0' && kFields[i].type == Type::Password) return true;
  if (strlen(value) > (size_t)kFields[i].max) return false;

  strncpy(s_strings[i].v, value, sizeof(s_strings[i].v) - 1);
  s_strings[i].v[sizeof(s_strings[i].v) - 1] = '\0';
  char sk[10];
  snprintf(sk, sizeof(sk), "s%u", (unsigned)i);
  s_prefs.putString(sk, s_strings[i].v);
  return true;
}

bool ap_mode() { return s_ap_mode; }
void set_ap_mode(bool on) { s_ap_mode = on; }

int32_t get(const char *key) {
  const int i = index_of(key);
  return i < 0 ? 0 : s_values[i];
}

bool set(const char *key, int32_t value) {
  const int i = index_of(key);
  if (i < 0) return false;
  if (value < kFields[i].min || value > kFields[i].max) return false;
  if (s_values[i] == value) return true;

  s_values[i] = value;
  char k[8];
  nvs_key(i, k, sizeof(k));
  s_prefs.putInt(k, value);
  return true;
}

void reset_defaults() {
  char k[10];
  for (size_t i = 0; i < kCount; i++) {
    s_values[i] = kFields[i].def;
    nvs_key(i, k, sizeof(k));
    s_prefs.putInt(k, kFields[i].def);

    if (kFields[i].sdef) {
      strncpy(s_strings[i].v, kFields[i].sdef, sizeof(s_strings[i].v) - 1);
      s_strings[i].v[sizeof(s_strings[i].v) - 1] = '\0';
      snprintf(k, sizeof(k), "s%u", (unsigned)i);
      s_prefs.putString(k, s_strings[i].v);
    }
  }
}

const Field *fields() { return kFields; }
size_t field_count() { return kCount; }

namespace v {
int alert_sound()          { return get("alert_sound"); }
int error_sound()          { return get("error_sound"); }
int done_sound()           { return get("done_sound"); }
int piezo_long_ms()        { return get("piezo_long_ms"); }
int piezo_repeats()        { return get("piezo_repeats"); }
int piezo_hz1()            { return get("piezo_hz1"); }
int piezo_hz2()            { return get("piezo_hz2"); }
int piezo_duty()           { return get("piezo_duty"); }
int piezo_gap_ms()         { return get("piezo_gap_ms"); }
int piezo_repeat_gap_ms()  { return get("piezo_rep_gap_ms"); }

int bell_pin()             { return get("bell_pin"); }

int style()                { return get("style"); }
int orange()               { return get("orange"); }
int eye_w()                { return get("eye_w"); }
int eye_h()                { return get("eye_h"); }
int eye_gap()              { return get("eye_gap"); }
int eye_center_y()         { return get("eye_center_y"); }
int grid_q()               { return get("grid_q"); }

int blink_min_ms()         { return get("blink_min_ms"); }
int blink_max_ms()         { return get("blink_max_ms"); }

int bored_after_min()      { return get("bored_after_min"); }
int sleep_after_min()      { return get("sleep_after_min"); }
int excited_s()            { return get("excited_s"); }
int alert_max_s()          { return get("alert_max_s"); }
int angry_s()              { return get("angry_s"); }
int poke_count()           { return get("poke_count"); }
int poke_window_s()        { return get("poke_window_s"); }
int window_hours()         { return get("window_hours"); }
bool swipe_swap_axes()     { return get("swipe_swap_axes") != 0; }

int backlight_sleeping()   { return get("bl_sleeping"); }
int backlight_bored()      { return get("bl_bored"); }
int backlight_normal()     { return get("bl_normal"); }
int backlight_excited()    { return get("bl_excited"); }
}  // namespace v

}  // namespace config
