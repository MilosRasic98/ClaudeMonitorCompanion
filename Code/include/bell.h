#pragma once

#include "mood.h"

// The audible alert: an external mechanical striker on LEDC PWM.
//
// The actuator, its drive stage and which GPIO it lands on are all still to be
// decided, so strike() logs and does nothing until PIN_BELL is set. begin() has
// a job either way — it holds the onboard piezo low, which is not optional.

namespace bell {

void begin();

// Play whatever the user has chosen for this cue: the striker, the buzzer,
// both, or nothing. Non-blocking; the pulse and the tone are ended by tick().
//
// The buzzer has two shapes rather than two pitches — a short double chirp for
// good news and one long tone for bad. You should be able to tell them apart
// with your back to the desk.
void play(mood::Cue cue);

// The individual pieces, for the settings page's test buttons.
void strike();
void celebrate();
void long_buzz();

// Call from loop() to end a pulse. Cheap when idle.
void tick();

// Re-read the striker GPIO from config. No-op if it has not changed.
void reconfigure();

bool wired();

}  // namespace bell
