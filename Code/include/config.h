#pragma once
#include <stddef.h>
#include <stdint.h>

// Runtime configuration, stored in NVS and edited from the board's own web
// page. The goal is that flashing once is the last time anyone touches
// firmware: every value here would otherwise be a #define you had to rebuild
// to change.
//
// The table in config.cpp is the single source of truth. Adding a setting is
// one row there — the JSON, the parser and the web form all derive from it, so
// nothing else needs editing, including the page itself.

namespace config {

enum class Type : uint8_t { Bool, Int, Enum, Text, Password };

struct Field {
  const char *group;
  const char *key;
  const char *label;
  Type type;
  int32_t min;
  int32_t max;
  int32_t def;
  const char *hint;     // nullptr for none
  const char *options;  // Enum only: comma-separated, e.g. "plain,grin"
  const char *sdef;     // Text/Password only: compiled-in default
};

void begin();

// Current value, by key. Unknown keys return 0 — callers use the accessors
// below rather than raw keys, so that only happens on a typo in the table.
int32_t get(const char *key);

// Returns false if the key is unknown or the value is out of range. Persists
// immediately: a settings page that silently loses changes on reboot is worse
// than no settings page.
bool set(const char *key, int32_t value);

// Text settings live alongside the numeric ones but in their own store, since
// NVS keeps strings and ints separately. A Password field never reads back:
// get_str() returns the real value for firmware use, but the JSON emitter
// sends an empty string, and an empty POST value means "leave it alone".
const char *get_str(const char *key);
bool set_str(const char *key, const char *value);

void reset_defaults();

// True when the configured network could not be joined and the board is
// hosting its own access point instead.
bool ap_mode();
void set_ap_mode(bool on);

// The table, for the JSON emitter.
const Field *fields();
size_t field_count();

// Named accessors. Everything reading config goes through these so a rename in
// the table is a compile error rather than a silent zero.
namespace v {
bool piezo_enabled();
int piezo_repeats();
int piezo_hz1();
int piezo_hz2();
int piezo_duty();
int piezo_gap_ms();
int piezo_repeat_gap_ms();

bool bell_on_alert();
bool bell_on_error();
int bell_pin();

int style();
int orange();
int eye_w();
int eye_h();
int eye_gap();
int eye_center_y();
int grid_q();

int blink_min_ms();
int blink_max_ms();

int bored_after_min();
int sleep_after_min();
int excited_s();
int alert_max_s();
int angry_s();
int poke_count();
int poke_window_s();

int backlight_sleeping();
int backlight_bored();
int backlight_normal();
int backlight_excited();
}  // namespace v

}  // namespace config
