#pragma once

// The audible alert: an external mechanical striker on LEDC PWM.
//
// The actuator, its drive stage and which GPIO it lands on are all still to be
// decided, so strike() logs and does nothing until PIN_BELL is set. begin() has
// a job either way — it holds the onboard piezo low, which is not optional.

namespace bell {

void begin();

// One strike of the external striker. Non-blocking; ended by tick().
void strike();

// Two rising chirps on the onboard piezo. Also non-blocking.
void celebrate();

// Call from loop() to end a pulse. Cheap when idle.
void tick();

// Re-read the striker GPIO from config. No-op if it has not changed.
void reconfigure();

bool wired();

}  // namespace bell
