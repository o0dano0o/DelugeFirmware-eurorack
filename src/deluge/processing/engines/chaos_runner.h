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
/// Whether chaos is in use at all: an output driven by chaos, or a gate whose Euclid hits are thinned by chaos
bool inUse();

/// Chaos-driven hit probability for a gate that is still playing its Euclidean pattern. Source 0 is off; 1 to 3
/// takes the x of sloth A, B or C. The depth (0 to 1, shared by all gates) sets how far the chaos pulls the
/// probability down from 100%. Ignored for a gate that chaos drives directly.
void setProbSource(int32_t gate, uint8_t source);
uint8_t getProbSource(int32_t gate);
void setProbDepth(float depth);
float getProbDepth();
/// Called for each hit on a gate; true if it should sound. Always true when the gate has no probability source.
bool allowNoteOn(int32_t gate);

/// Free time: periods in seconds. Tempo time: periods in beats, so the chaos follows the song's tempo.
void setTempoSync(bool sync);
bool isTempoSync();

/// Everything that is saved with a song
struct Settings {
	Model model = Model::LORENZ;
	bool tempoSync = false;
	float periods[kNumSloths] = {12.0f, 30.0f, 90.0f};
	float coupling = 0.04f;
	uint8_t gateMask = 0; ///< bit g: gate g driven by chaos
	uint8_t cvMask = 0;   ///< bit c: CV c driven by chaos
	uint8_t probSource[kNumGateRules] = {0, 0, 0, 0}; ///< per gate: 0 off, 1 to 3 = sloth A to C
	float probDepth = 0.5f;
};
Settings getSettings();
/// Replaces the current settings (and the outputs' sources) with these
void loadSettings(const Settings& settings);
/// Chaos off and everything back to the defaults, as for a song with no chaos settings
void resetToDefaults();

/// For displays: true while chaos gate rule `gate` is high, or for `holdMs` after it last went high. Triggers are only
/// 10 ms long, so without the hold a display redrawn every 80 ms would miss most of them.
bool gateRecentlyHigh(int32_t gate, int32_t holdMs);

/// Called from the scheduler (about every 2 to 4 ms)
void tick();

/// Called when playback starts; restarts the chaos from its seed so each play repeats
void playbackBegun();

/// Song tempo in BPM (a fallback value if there is no song)
float getBpm();

ChaosEngine& engine();

} // namespace deluge::chaos::runner
