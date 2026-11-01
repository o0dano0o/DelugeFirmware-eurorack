/*
 * Copyright © 2026 The Deluge Firmware contributors
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "processing/engines/turing_engine.h"

/// Glue between the TuringEngine and the hardware. The Turing machine drives one CV/gate channel pair like a voice:
/// the CV is a pitch (quantised to the song's scale, 1V/octave as the channel is set up) and the gate fires on each
/// step. While it is on, nothing else (Euclid, chaos) writes to that CV or gate.
namespace deluge::turing::runner {

enum class ClockSource : uint8_t {
	SONG,       ///< the song clock, stepping every `division` while playing
	CHAOS_LOBE, ///< each chaos lobe-flip trigger (gate rule 2)
	CHAOS_LOOP, ///< each chaos big-loop trigger (gate rule 3)
	kNumSources,
};

enum class RandomSource : uint8_t {
	RNG,   ///< a pseudo-random number generator
	CHAOS, ///< derived from the chaos engine's state
	kNumSources,
};

constexpr int32_t kNumDivisions = 9;
constexpr int32_t kHistoryLength = 16;
constexpr int32_t kMinOctaves = 1;
constexpr int32_t kMaxOctaves = 4;

/// Name of a clock division for displays ("1/8", "1 bar", ...)
char const* divisionName(int32_t division);

struct Settings {
	int8_t channel = -1; ///< 0 or 1 for CV/gate 1 or 2; -1 means the Turing machine is off
	ClockSource clock = ClockSource::SONG;
	uint8_t division = 1; ///< index into the division table, 1 is an eighth note
	RandomSource random = RandomSource::RNG;
	float flipProbability = 0.1f;
	uint8_t length = 8;
	uint8_t octaves = 2;
	uint16_t shiftRegister = 0;
	bool hasRegister = false; ///< false until a register has been made or loaded
};

Settings getSettings();
void loadSettings(const Settings& settings);
void resetToDefaults();

/// Turns the Turing machine on for a channel (0 or 1), or off with -1
void setChannel(int32_t channel);
int32_t getChannel();
bool isActive();

void setClockSource(ClockSource source);
ClockSource getClockSource();
void setDivision(int32_t division);
int32_t getDivision();
void setRandomSource(RandomSource source);
RandomSource getRandomSource();
void setFlipProbability(float probability);
void setLength(int32_t length);
void setOctaves(int32_t octaves);
int32_t getOctaves();
/// Fills the register with new random bits
void randomiseRegister();

TuringEngine& engine();

/// The value (0 to 255) output `age` steps ago; age 0 is the latest. 0 if there isn't one that old.
uint8_t getHistoryValue(int32_t age);

/// Counts clock steps, for displays that want to react to each one
uint32_t getStepCount();
/// Seconds-ish check for displays: true for a short time after each step
bool stepRecently(int32_t holdMs);

/// Called from the scheduler
void tick();
/// Called when playback starts, so the song clock steps on the first tick
void playbackBegun();

} // namespace deluge::turing::runner
