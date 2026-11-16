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

#include "processing/engines/rungler_engine.h"

/// Glue between the RunglerEngine and the hardware. Any CV output or gate can be given to Rungler (through the
/// Eurorack overview or the Rungler view); while it owns an output nothing else writes to it. Rungler never takes an
/// output another generator already holds.
namespace deluge::rungler::runner {

struct Settings {
	Params params;
	uint8_t cvMask = 0;   ///< bit n set: CV n+1 is Rungler
	uint8_t gateMask = 0; ///< bit n set: gate n+1 is Rungler
};

Settings getSettings();
void loadSettings(const Settings& settings);
void resetToDefaults();

bool isActive();

bool isCVRungler(int32_t cv);
bool isGateRungler(int32_t gate);
void setCVRungler(int32_t cv, bool on);
void setGateRungler(int32_t gate, bool on);
/// Gives Rungler every output that is free (or turns all of its outputs off)
void setAll(bool on);

RunglerEngine& engine();
/// Change settings through these so the song knows about them
void setTempoSync(bool sync);
bool isTempoSync();

/// True for a short time after gate g (0 to 3) goes high, so displays can show short pulses
bool gateRecentlyHigh(int32_t gate, int32_t holdMs);
uint32_t getStepCount();
/// True for a short time after each clock step
bool stepRecently(int32_t holdMs);

/// Called from the scheduler
void tick();
/// Called when playback starts: the loops restart from their first step
void playbackBegun();

} // namespace deluge::rungler::runner
