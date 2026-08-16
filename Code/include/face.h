#pragma once
#include <stdint.h>

#include "mood.h"
#include "styles.h"

// The face: an orange field with two black bars on it, per the printed mascot —
// plus the spectacled, pixel-glasses and shades variants it also comes in.

namespace face {

void begin();

// A tap. The eyes shake it off for a moment, whatever mood is running.
void poke();

// Re-read style, colour and geometry from the config store and repaint. Called
// after the web page changes a setting. Safe from any task — the work happens
// at the top of the next frame.
void refresh();

// One frame. Computes eye geometry from the mood, quantises it to the grid, and
// repaints only what changed.
void render(mood::Mood m, uint8_t energy, uint32_t now_ms);

// Cycles the on-screen orange through the candidates in tuning.h. The screen
// colour and the filament colour will not match by hex — one is emissive, the
// other diffuse — so this gets picked by eye against the actual shell.
void next_orange();
uint8_t orange_index();

// Swipe left/right on the panel, or [ and ] over serial. The choice survives a
// reboot — it is a preference, not a setting you want to re-pick every power
// cycle.
void cycle_style(int delta);

// The current field colour — orange normally, red while the mascot wants
// something. The other screens share it so the whole device reads as one thing.
uint16_t background();
Style style();
const char *style_name();

}  // namespace face
