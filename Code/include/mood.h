#pragma once
#include <stdint.h>

// The mood engine. The board owns this, not the host.
//
// The host reports raw facts — a prompt was submitted, a turn finished — and
// this decides how the face feels about them. That split is not stylistic:
// BORED and ASLEEP are the *absence* of events, and no hook can push a
// non-event. Keeping the state machine here also means the face keeps behaving
// correctly when the host sleeps, quits, or the Wi-Fi drops.

namespace mood {

enum class Mood : uint8_t {
  Sleeping,
  Waking,
  Chill,
  Bored,
  Working,
  Excited,
  Confused,
  Angry,
  Limit,   // usage limit hit — nothing will work until it resets
};

// Raw facts from the host, plus Ack which comes from the touchscreen.
enum class Event : uint8_t {
  PromptSubmitted,
  TurnFinished,
  NeedsInput,
  Idle,
  Error,
  SessionStarted,
  SessionEnded,
  LimitReached,  // API returned a rate limit; the session is stuck
  Answered,  // the human dealt with the thing that was blocking
  Ack,
};

void begin(uint32_t now_ms);

// Safe to call from any task — events are queued, not applied inline.
void post(Event e);

// Drains the queue and advances timers. Call once per frame.
void tick(uint32_t now_ms);

// Jump straight to a mood, bypassing the event logic. For tuning only — waiting
// half an hour to see what SLEEPING looks like is not a workflow.
void force(Mood m, bool sticky);

Mood current();
const char *name(Mood m);
uint8_t energy();            // 0..100
uint32_t in_mood_ms();       // since the current mood was entered

// True while the mascot actively wants something from the human — an
// unacknowledged "needs input", or a fit of pique. Drives the red field.
bool alerting();

// True exactly once per alert that warrants an audible strike: anything that
// turns the field red (a block, or being poked past patience) plus errors.
bool take_bell();

// True exactly once when a turn finishes. Distinct from take_bell() because it
// is a celebration, not a demand — it gets the onboard piezo, not the striker.
bool take_celebration();

}  // namespace mood
