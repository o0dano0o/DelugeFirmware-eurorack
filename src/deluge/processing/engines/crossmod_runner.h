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

#include "processing/engines/crossmod_engine.h"
#include <cstdint>

/// Glue for cross-modulation: every tick it reads the sources (one number from each running generator) and moves the
/// settings that routes point at. Sources and targets are listed here, in one place.
namespace deluge::crossmod::runner {

constexpr int32_t kNumSources = 20;
constexpr int32_t kNumTargets = 19;

/// Full names for the OLED, and four-character tags for the 7-segment display
char const* sourceName(int32_t source);
char const* sourceTag(int32_t source);
char const* targetName(int32_t target);
char const* targetTag(int32_t target);
/// A colour family for the pads: 0 chaos, 1 Marbles, 2 Rungler, 3 Neurons, 4 Turing, 5 Euclid
int32_t sourceFamily(int32_t source);
int32_t targetFamily(int32_t target);
/// True if the generator that owns this source / target is running right now
bool sourceRunning(int32_t source);
bool targetRunning(int32_t target);

struct Settings {
	Route routes[kNumRoutes];
};
Settings getSettings();
void loadSettings(const Settings& settings);
void resetToDefaults();
bool isUsed();

CrossmodEngine& engine();

/// The current value of a source, 0 to 1 (0.5 if that generator is not running)
float sourceValue(int32_t source);

/// Called by the CV engine whenever a Euclid (note) hit plays on a gate, so Euclid hits can be used as a source
void noteHit(int32_t gate);

/// Called from the scheduler
void tick();

} // namespace deluge::crossmod::runner
