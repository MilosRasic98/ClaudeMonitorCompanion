// MR26 Claude Notifier — mascot face.
//
// A Waveshare ESP32-S3-Touch-LCD-1.69 sitting in a 3D-printed shell shaped like
// the Claude mascot. The screen is its face: an orange field with two black
// bars for eyes, whose mood mirrors what Claude Code is doing on the computer.
//
// Claude Code hooks POST bare event names at the board over the LAN; the mood
// engine here decides what the face does about them. Serial commands drive the
// same events by hand, and force individual moods, so the animation can be
// tuned without the host in the loop. See README.md.

#include <Arduino.h>
#include <Wire.h>

#include "bell.h"
#include "config.h"
#include "display.h"
#include "face.h"
#include "mood.h"
#include "net.h"
#include "pins.h"
#include "touch.h"
#include "usage.h"
#include "tuning.h"
#include "views.h"

namespace {

TaskHandle_t s_render_task = nullptr;
bool s_touch_ok = false;

// Swipe cycles the face style; a tap acknowledges an alert and silences the
// bell, the same thing the host's ack would do.
//
// Both are derived here from the raw coordinate stream rather than from the
// CST816's gesture register. The chip only decides a gesture happened once the
// finger lifts, and it latches the code afterwards, so a second swipe the same
// way reads as unchanged and is dropped — the exact motion someone makes when
// flicking through styles. Watching the coordinates instead means the swipe
// fires the moment it passes the threshold, mid-drag, which also feels quicker.
struct Drag {
  bool down;
  bool fired;
  int16_t x0, y0;
  uint32_t t0;
};
Drag s_drag = {};

void service_touch() {
  if (!s_touch_ok) return;

  touch::Event e;
  while (touch::poll(e)) {
    if (e.pressed && !s_drag.down) {
      s_drag.down = true;
      s_drag.fired = false;
      s_drag.x0 = e.x;
      s_drag.y0 = e.y;
      s_drag.t0 = millis();
      continue;
    }

    const int dx = e.x - s_drag.x0;
    const int dy = e.y - s_drag.y0;

    if (e.pressed) {
      if (s_drag.fired) continue;
      const int adx = abs(dx), ady = abs(dy);
      if (max(adx, ady) < SWIPE_MIN_PX) continue;
      // Whichever axis dominates wins. The raw-to-panel orientation is still
      // unconfirmed, so keying off the dominant axis rather than X alone means
      // a horizontal flick works even if the driver's axes turn out swapped.
      // Sideways changes the face, up-down changes the screen. If the touch
      // controller's axes turn out swapped on a given panel, one setting flips
      // the interpretation rather than needing a rebuild.
      bool horizontal = adx >= ady;
      if (config::v::swipe_swap_axes()) horizontal = !horizontal;
      const int travel = horizontal ? dx : dy;
      const int dir = (travel > 0) ? 1 : -1;
      if (horizontal) {
        face::cycle_style(SWIPE_INVERT ? -dir : dir);
      } else {
        views::cycle(dir);
      }
      Serial.printf("swipe dx %d dy %d -> %s\n", dx, dy,
                    horizontal ? "style" : "view");
      s_drag.fired = true;
      continue;
    }

    // Released.
    if (s_drag.down && !s_drag.fired) {
      const uint32_t held = millis() - s_drag.t0;
      if (held <= TAP_MAX_MS && abs(dx) <= TAP_MAX_PX && abs(dy) <= TAP_MAX_PX) {
        Serial.printf("tap %d,%d\n", e.x, e.y);
        face::poke();
        mood::post(mood::Event::Ack);
      }
    }
    s_drag.down = false;
  }
}

void i2c_scan() {
  Serial.println("I2C scan on SDA=11 SCL=10:");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;
    const char *guess = "?";
    switch (addr) {
      case 0x15: guess = "CST816 touch"; break;
      case 0x51: guess = "PCF85063 RTC"; break;
      case 0x6A:
      case 0x6B: guess = "QMI8658C IMU"; break;
      case 0x70: guess = "SHTC3 temp/humidity"; break;
    }
    Serial.printf("  0x%02X  %s\n", addr, guess);
    found++;
  }
  if (found == 0) Serial.println("  nothing found");
}

// Rendering gets its own task on core 1. Wi-Fi and lwIP run at priorities 23
// and 18 on core 0 and can hold a core for milliseconds during association or
// DHCP — sharing a core with them would drop frames exactly when a notification
// arrives, which is the one moment the face has to look good.
void render_task(void *) {
  TickType_t last = xTaskGetTickCount();
  uint32_t frames = 0, busy_us = 0;
  uint32_t report_at = millis() + 10000;

  for (;;) {
    const uint32_t now = millis();
    const uint32_t t0 = micros();

    mood::tick(now);

    // The data screens own the whole panel when they are up. Coming back to the
    // face needs an explicit repaint, since its dirty tracking has no idea
    // something else drew over it.
    static views::View last_view = views::View::Face;
    const bool handled = views::render(now, face::background());
    if (views::current() != last_view) {
      last_view = views::current();
      if (last_view == views::View::Face) face::refresh();
    }
    if (!handled) face::render(mood::current(), mood::energy(), now);

    if (mood::take_bell()) bell::strike();
    if (mood::take_celebration()) bell::celebrate();

    busy_us += micros() - t0;
    frames++;

    if (now >= report_at) {
      Serial.printf("%-8s energy %3u   %lu fps, %lu us/frame busy\n",
                    mood::name(mood::current()), mood::energy(),
                    (unsigned long)(frames / 10),
                    (unsigned long)(frames ? busy_us / frames : 0));
      frames = 0;
      busy_us = 0;
      report_at = now + 10000;
    }

    vTaskDelayUntil(&last, pdMS_TO_TICKS(FRAME_PERIOD_MS));
  }
}

// Auto-cycle through every mood, for judging the animations without having to
// drive real events at the board.
bool s_demo = false;
uint32_t s_demo_next = 0;
uint8_t s_demo_step = 0;

const mood::Mood kAllMoods[] = {
    mood::Mood::Chill,   mood::Mood::Working,  mood::Mood::Excited,
    mood::Mood::Confused, mood::Mood::Angry,   mood::Mood::Limit,
    mood::Mood::Bored,    mood::Mood::Sleeping, mood::Mood::Waking,
};
const uint8_t kMoodCount = sizeof(kAllMoods) / sizeof(kAllMoods[0]);

void service_demo() {
  if (!s_demo || millis() < s_demo_next) return;
  const mood::Mood m = kAllMoods[s_demo_step % kMoodCount];
  s_demo_step++;
  mood::force(m, /*sticky=*/true);
  Serial.printf("demo -> %s\n", mood::name(m));
  s_demo_next = millis() + 5000;
}

void print_help() {
  Serial.println("events : p prompt | f turn finished | n needs input");
  Serial.println("         e error  | i idle          | s session start");
  Serial.println("         x session end | a ack (touch) | k answered");
  Serial.println("moods  : 1 chill 2 working 3 excited 4 confused 5 angry");
  Serial.println("         6 limit 7 bored 8 sleeping 9 waking | d auto-cycle");
  Serial.println("views  : [ ] style (swipe sideways) | v next screen (swipe up)");
  Serial.println("tuning : c orange | b bell | z buzz | w scan | r reach | h health | ?");
}

void handle_command(char c) {
  switch (c) {
    case 'p': mood::post(mood::Event::PromptSubmitted); break;
    case 'f': mood::post(mood::Event::TurnFinished);    break;
    case 'n': mood::post(mood::Event::NeedsInput);      break;
    case 'e': mood::post(mood::Event::Error);           break;
    case 'i': mood::post(mood::Event::Idle);            break;
    case 's': mood::post(mood::Event::SessionStarted);  break;
    case 'x': mood::post(mood::Event::SessionEnded);    break;
    case 'a': mood::post(mood::Event::Ack);             break;
    case 'k': mood::post(mood::Event::Answered);        break;
    case '1': case '2': case '3': case '4': case '5': case '6': case '7':
    case '8': case '9': {
      const mood::Mood m = kAllMoods[c - '1'];
      mood::force(m, /*sticky=*/true);
      Serial.printf("forced %s\n", mood::name(m));
      return;
    }
    case 'd':
      s_demo = !s_demo;
      s_demo_next = 0;
      Serial.printf("demo cycle %s\n", s_demo ? "on" : "off");
      return;
    case 'c':
      face::next_orange();
      Serial.printf("orange candidate %u\n", face::orange_index());
      return;
    case '[':
    case ']':
      face::cycle_style(c == ']' ? +1 : -1);
      Serial.printf("style -> %s\n", face::style_name());
      return;
    // Not printing the name: the change is applied by the render task, so
    // reading it here would report the previous view.
    case 'v': views::cycle(1); Serial.println("next view"); return;
    case 'w': net::scan(); return;
    case 'r': net::probe(); return;
    case 'b': bell::strike(); return;
    case 'z': bell::celebrate(); Serial.println("piezo celebration"); return;
    case 'h':
      Serial.printf("%s/%s energy %u  wifi %s %s  heap %lu\n",
                    mood::name(mood::current()), face::style_name(),
                    mood::energy(),
                    net::connected() ? "up" : "down", net::ip(),
                    (unsigned long)ESP.getFreeHeap());
      return;
    case '?': print_help(); return;
    default: return;
  }
  Serial.printf("-> %c\n", c);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);

  Serial.println();
  Serial.println("=== MR26 Claude Notifier ===");
  Serial.printf("chip  : %s rev %d @ %lu MHz\n", ESP.getChipModel(),
                ESP.getChipRevision(), (unsigned long)getCpuFrequencyMhz());
  Serial.printf("flash : %lu bytes   PSRAM: %lu bytes\n",
                (unsigned long)ESP.getFlashChipSize(),
                (unsigned long)ESP.getPsramSize());

  if (ESP.getPsramSize() == 0) {
    Serial.println("WARNING: no PSRAM — check memory_type = qio_opi");
  }

  config::begin();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  i2c_scan();

  if (!display::begin()) {
    Serial.println("FATAL: display init failed");
    while (true) delay(1000);
  }

  usage::begin();
  views::begin();
  face::begin();
  mood::begin(millis());
  bell::begin();

  s_touch_ok = touch::begin();
  Serial.printf("touch : %s (chip id 0x%02X)\n",
                s_touch_ok ? "ready" : "NOT RESPONDING", touch::chip_id());

  Serial.printf("face  : style %s, orange %u\n", face::style_name(),
                face::orange_index());

  net::begin();
  print_help();

  xTaskCreatePinnedToCore(render_task, "render", 4096, nullptr, 3,
                          &s_render_task, 1);
}

void loop() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c > ' ') handle_command((char)c);
  }
  // Touch is serviced here rather than in the render task: the CST816 shares
  // the I2C bus with the RTC and IMU, and I2C stalls must never land inside the
  // frame budget.
  service_touch();
  service_demo();
  bell::tick();
  net::tick();
  usage::tick(net::connected());
  // 5 ms, not 20: this is the swipe sampling rate, and a flick that only gets
  // sampled three times does not read as a flick.
  delay(5);
}
