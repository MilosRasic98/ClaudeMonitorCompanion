#include "mood.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "config.h"
#include "tuning.h"

namespace mood {
namespace {

QueueHandle_t s_queue = nullptr;

Mood s_mood = Mood::Chill;
uint32_t s_mood_entered = 0;

uint32_t s_last_event = 0;    // any event at all — drives sleep
uint32_t s_last_prompt = 0;   // user prompts only — drives boredom
uint32_t s_turn_started = 0;  // for the stuck-turn safety net
bool s_turn_running = false;

// Excited and Confused come in two flavours: a timed celebration that decays on
// its own, and a sticky alert that waits to be acknowledged.
bool s_sticky = false;
bool s_bell_pending = false;
bool s_celebration_pending = false;

int32_t s_energy = 0;
uint32_t s_energy_last_decay = 0;

// Ring of recent tap times. If the whole ring fits inside the window, the
// mascot has been poked once too often. Sized for the config maximum; only the
// first poke_count() slots are used, so the threshold is changeable at runtime.
const uint8_t kMaxPokes = 20;
uint32_t s_pokes[kMaxPokes] = {};
uint8_t s_poke_idx = 0;

void enter(Mood m, uint32_t now, bool sticky = false) {
  if (s_mood == m && s_sticky == sticky) return;
  s_mood = m;
  s_sticky = sticky;
  s_mood_entered = now;
}

void bump_energy(int amount) {
  s_energy += amount;
  if (s_energy > ENERGY_MAX) s_energy = ENERGY_MAX;
}

void apply(Event e, uint32_t now) {
  s_last_event = now;

  switch (e) {
    case Event::PromptSubmitted:
      if (s_mood == Mood::Limit) s_sticky = false;  // work resumed
      bump_energy(ENERGY_BUMP_PROMPT);
      s_last_prompt = now;
      s_turn_running = true;
      s_turn_started = now;
      enter(Mood::Working, now);
      break;

    case Event::TurnFinished:
      bump_energy(ENERGY_BUMP_EVENT);
      s_turn_running = false;
      // A finished turn is a celebration, not an alert: it times out by itself.
      enter(Mood::Excited, now, /*sticky=*/false);
      s_celebration_pending = true;
      break;

    case Event::NeedsInput:
      bump_energy(ENERGY_BUMP_EVENT);
      // Deliberately does NOT clear s_turn_running. A permission prompt or a
      // question happens *during* a turn — Claude is paused, not finished — so
      // answering it should drop back into WORKING, not CHILL.
      enter(Mood::Excited, now, /*sticky=*/true);
      if (config::v::bell_on_alert()) s_bell_pending = true;
      break;

    case Event::LimitReached:
      // The most sticky state there is. Nothing the human does at the keyboard
      // fixes it, so it clears only on a tap or when work resumes.
      s_turn_running = false;
      enter(Mood::Limit, now, /*sticky=*/true);
      if (config::v::bell_on_alert()) s_bell_pending = true;
      break;

    case Event::Answered:
      bump_energy(ENERGY_BUMP_EVENT / 2);
      s_sticky = false;
      s_bell_pending = false;
      enter(s_turn_running ? Mood::Working : Mood::Chill, now);
      break;

    case Event::Error:
      bump_energy(ENERGY_BUMP_EVENT);
      s_turn_running = false;
      enter(Mood::Confused, now, /*sticky=*/false);
      if (config::v::bell_on_error()) s_bell_pending = true;
      break;

    case Event::Idle:
      if (!s_turn_running && !s_sticky) enter(Mood::Chill, now);
      break;

    case Event::SessionStarted:
      bump_energy(ENERGY_BUMP_EVENT);
      enter(s_mood == Mood::Sleeping ? Mood::Waking : Mood::Chill, now);
      break;

    case Event::SessionEnded:
      s_turn_running = false;
      if (!s_sticky) enter(Mood::Chill, now);
      break;

    case Event::Ack: {
      // Clears any alert and counts as a small sign of life, but deliberately
      // does not reset the boredom clock — petting the mascot is not a prompt.
      s_sticky = false;
      s_bell_pending = false;
      bump_energy(ENERGY_BUMP_EVENT / 2);

      const uint8_t need =
          (uint8_t)constrain(config::v::poke_count(), 2, (int)kMaxPokes);
      s_pokes[s_poke_idx] = now;
      s_poke_idx = (uint8_t)((s_poke_idx + 1) % need);
      // The oldest entry in the ring is `need` taps ago. If even that one is
      // recent, the whole burst landed inside the window.
      const uint32_t oldest = s_pokes[s_poke_idx];
      if (oldest != 0 &&
          now - oldest <= (uint32_t)config::v::poke_window_s() * 1000UL) {
        for (uint32_t &t : s_pokes) t = 0;  // don't re-trigger on the next tap
        enter(Mood::Angry, now);
        if (config::v::bell_on_alert()) s_bell_pending = true;
        break;
      }

      if (s_mood != Mood::Angry) {
        enter(s_turn_running ? Mood::Working : Mood::Chill, now);
      }
      break;
    }
  }
}

}  // namespace

void begin(uint32_t now_ms) {
  s_queue = xQueueCreate(16, sizeof(uint8_t));
  s_mood = Mood::Chill;
  s_mood_entered = now_ms;
  s_last_event = now_ms;
  s_last_prompt = now_ms;
  s_energy_last_decay = now_ms;
}

void post(Event e) {
  if (!s_queue) return;
  const uint8_t v = (uint8_t)e;
  xQueueSend(s_queue, &v, 0);
}

void tick(uint32_t now) {
  uint8_t v;
  while (s_queue && xQueueReceive(s_queue, &v, 0) == pdTRUE) {
    apply((Event)v, now);
  }

  while (s_energy > 0 && now - s_energy_last_decay >= ENERGY_DECAY_MS) {
    s_energy--;
    s_energy_last_decay += ENERGY_DECAY_MS;
  }
  if (s_energy == 0) s_energy_last_decay = now;

  const uint32_t since_mood = now - s_mood_entered;

  switch (s_mood) {
    case Mood::Waking:
      if (since_mood > WAKE_MS) enter(Mood::Chill, now);
      break;

    case Mood::Excited:
      if (!s_sticky &&
          since_mood > (uint32_t)config::v::excited_s() * 1000UL) {
        enter(Mood::Chill, now);
      } else if (s_sticky &&
                 since_mood > (uint32_t)config::v::alert_max_s() * 1000UL) {
        // Safety net only. Something should have cleared this.
        s_sticky = false;
        enter(Mood::Chill, now);
      }
      break;

    case Mood::Confused:
      if (!s_sticky && since_mood > CONFUSED_MS) enter(Mood::Chill, now);
      break;

    case Mood::Angry:
      if (since_mood > (uint32_t)config::v::angry_s() * 1000UL) {
        enter(Mood::Chill, now);
      }
      break;

    case Mood::Working:
      // A session that dies mid-turn would otherwise leave the face working
      // forever, since nothing will ever report the turn finishing.
      if (s_turn_running && now - s_turn_started > WORKING_MAX_MS) {
        s_turn_running = false;
        enter(Mood::Chill, now);
      }
      break;

    default:
      break;
  }

  // Time-only decay. Alerts are exempt: something the human still has to deal
  // with should not quietly turn into boredom.
  if (!s_sticky && !s_turn_running && s_mood != Mood::Waking &&
      s_mood != Mood::Angry && s_mood != Mood::Limit) {
    if (now - s_last_event >
        (uint32_t)config::v::sleep_after_min() * 60UL * 1000UL) {
      enter(Mood::Sleeping, now);
    } else if (s_mood == Mood::Chill &&
               now - s_last_prompt >
                   (uint32_t)config::v::bored_after_min() * 60UL * 1000UL) {
      enter(Mood::Bored, now);
    }
  }
}

void force(Mood m, bool sticky) {
  const uint32_t now = millis();
  // Reset the clocks too, so the automatic decay does not immediately drag the
  // forced mood somewhere else while it is being looked at.
  s_last_event = now;
  s_last_prompt = now;
  s_turn_running = (m == Mood::Working);
  s_turn_started = now;
  s_mood = m;
  s_sticky = sticky;
  s_mood_entered = now;
}

bool alerting() {
  return s_mood == Mood::Angry || s_mood == Mood::Limit ||
         (s_mood == Mood::Excited && s_sticky);
}

Mood current() { return s_mood; }
uint8_t energy() { return (uint8_t)s_energy; }
uint32_t in_mood_ms() { return millis() - s_mood_entered; }

bool take_bell() {
  if (!s_bell_pending) return false;
  s_bell_pending = false;
  return true;
}

bool take_celebration() {
  if (!s_celebration_pending) return false;
  s_celebration_pending = false;
  return true;
}

const char *name(Mood m) {
  switch (m) {
    case Mood::Sleeping: return "SLEEPING";
    case Mood::Waking:   return "WAKING";
    case Mood::Chill:    return "CHILL";
    case Mood::Bored:    return "BORED";
    case Mood::Working:  return "WORKING";
    case Mood::Excited:  return "EXCITED";
    case Mood::Confused: return "CONFUSED";
    case Mood::Angry:    return "ANGRY";
    case Mood::Limit:    return "LIMIT";
  }
  return "?";
}

}  // namespace mood
