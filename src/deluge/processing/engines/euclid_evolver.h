/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "processing/engines/euclid_evolve_logic.h"
#include <cstdint>

/// Lets the Euclidean gate tracks change each other over time. A track can be set to listen to another track's hits
/// (or its own); every few hits it nudges its number of hits or its rotation, moving up, down, bouncing or wandering.
namespace deluge::euclid::evolver {

constexpr int32_t kNumTracks = 4;

const EvolveRule& rule(int32_t track);
void setRule(int32_t track, const EvolveRule& rule);
/// Back to nothing listening, nothing frozen
void reset();

/// While frozen, hits are still counted but nothing changes
bool isFrozen();
void setFrozen(bool frozen);

/// Puts a track back to the number of hits and rotation it had when it was last set listening
void restoreBaseline(int32_t track);

/// Goes up every time any track is changed, so views know to redraw
uint32_t changeCount();

/// Current hits and rotation of a track's pattern, for displays (false if the clip has no row for it)
bool readTrack(int32_t track, int32_t& hits, int32_t& rotation, int32_t& span);

/// Called regularly from the scheduler
void tick();
void playbackBegun();

} // namespace deluge::euclid::evolver
