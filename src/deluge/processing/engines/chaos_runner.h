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

#include "processing/engines/chaos_engine.h"

/// Glue between the pure ChaosEngine and the hardware: a scheduler task steps the engine in real time and writes the
/// result to the CV outputs and, optionally, the gates (gate N follows rule N).
namespace deluge::chaos::runner {

/// Which source drives each output. By default gate N follows chaos gate rule N. Switching a gate or CV to chaos
/// silences whatever else was driving it (the Euclidean pattern stays in the clip and comes back when switched back).
void setGateChaos(int32_t gate, bool chaos);
bool isGateChaos(int32_t gate);
void setCVChaos(int32_t cv, bool chaos);
bool isCVChaos(int32_t cv);
/// Everything to chaos (CV 1, CV 2, gates 1 to 4), or everything back to normal
void setAll(bool chaos);
/// Whether any output is driven by chaos
bool isActive();

/// Free time: periods in seconds. Tempo time: periods in beats, so the chaos follows the song's tempo.
void setTempoSync(bool sync);
bool isTempoSync();

/// Called from the scheduler (about every 2 to 4 ms)
void tick();

/// Called when playback starts; restarts the chaos from its seed so each play repeats
void playbackBegun();

/// Song tempo in BPM (a fallback value if there is no song)
float getBpm();

ChaosEngine& engine();

} // namespace deluge::chaos::runner
