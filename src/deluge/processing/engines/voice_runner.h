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

#include "processing/engines/phrase_loop.h"
#include <cstdint>

/// Live voice-to-CV: sing or hum into the Deluge's mic or line input and get the pitch on a CV output with a gate on
/// the matching gate output.
namespace deluge::voice::runner {

constexpr int32_t kNumSensitivities = 6;

/// How a recorded phrase is timed
enum class LoopTiming : uint8_t {
	FREE, ///< your own timing, the length is the time between the two presses of RECORD
	SONG, ///< snapped to sixteenth-note steps of the song clock (needs the song playing)
};

struct Settings {
	int8_t channel = -1;      ///< CV channel (0 or 1, its gate is the same number), -1 for off
	bool quantise = true;     ///< true: snap to semitones, false: follow the pitch continuously (glides)
	int8_t octaveShift = 0;   ///< -2 to +2 octaves added to the output
	uint8_t sensitivity = 2;  ///< 0 (needs a loud voice) to kNumSensitivities - 1 (picks up quiet sounds)
	LoopTiming loopTiming = LoopTiming::FREE;
	bool loopOneShot = false; ///< a triggered phrase plays once instead of looping
	int8_t loopTrigger = -1;  ///< gate 0 to 3 whose rising edges restart the phrase, -1 for none
};

const Settings& getSettings();
void loadSettings(const Settings& s);
void resetToDefaults();

/// Selects the output channel (0 or 1), or -1 to switch the tracker off. While off it costs nothing.
void setChannel(int32_t channel);
int32_t getChannel();
bool isActive();

void setQuantise(bool quantise);
void setOctaveShift(int32_t octaves);
void setSensitivity(int32_t sensitivity);

/// Called from the audio engine with interleaved stereo input (as the codec delivers it) to tap the left channel. Does
/// nothing while the tracker is off.
void feedInput(const int32_t* interleavedStereo, int32_t numFrames);

/// Scheduler task: analyses what has arrived and drives the CV and gate
void tick();

/// For the display
bool gateOn();
int32_t currentNote();       ///< MIDI note number of the held note, -1 before anything was sung
float currentLevel();        ///< 0 to 1
bool recentlyVoiced();       ///< true for a moment after a pitch was last heard

// ---- phrase looper: record what you sing, then replay it on the same CV and gate ----

enum class RecordResult : uint8_t {
	NOT_ACTIVE,     ///< switch the voice tracker on first
	NEEDS_PLAYBACK, ///< song-clock timing needs the song playing
	STARTED,        ///< recording
	PLAYING,        ///< recording ended and the phrase is now playing
	NOTHING_SUNG,   ///< recording ended with nothing in it
};
enum class LoopState : uint8_t { NONE, RECORDING, PLAYING, READY };

/// RECORD pressed: starts a recording, or ends it and plays the phrase
RecordResult toggleRecord();
/// Plays the phrase from the top (a pad press or a trigger); does nothing without a phrase
void triggerLoop();
/// Stops the phrase and goes back to the live voice
void stopLoop();
/// Throws the phrase away
void clearLoop();
void setLoopTiming(LoopTiming timing);
void setLoopOneShot(bool oneShot);
/// Gate 0 to 3 (not the voice tracker's own) whose triggers restart the phrase, or -1
void setLoopTrigger(int32_t gate);
LoopState loopState();
/// 0 to 1: where the playback is in the phrase
float loopPosition();
/// The note sounding in the phrase at a fraction 0 to 1 of its length, or -1 for silence
int32_t loopNoteAt(float fraction);
const PhraseLoop& loopPhrase();
void loadLoop(const PhraseEvent* events, int32_t count, uint32_t length);
/// Called when song playback begins, so a song-clock phrase starts in time
void playbackBegun();

/// Note history for the display: kHistoryLength slots, each about a quarter of a second, the newest last.
constexpr int32_t kHistoryLength = 16;
/// The note sung during slot `age` slots ago (0 is the slot in progress), or -1 for silence
int32_t historyNote(int32_t age);
/// Increases every time the history changes, for cheap redraw checks
uint32_t historyVersion();

} // namespace deluge::voice::runner
