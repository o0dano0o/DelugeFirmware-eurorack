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

#include "definitions_cxx.hpp"
#include "gui/views/clip_view.h"
#include "hid/button.h"

class InstrumentClip;
class NoteRow;
class ModelStackWithNoteRow;
class ModelStackWithTimelineCounter;

/// A Eurorack-oriented view of a Kit's four gate outputs, modelled on a Euclidean-circles style module.
///
/// All four gate tracks (the Kit rows holding a GateDrum for gate channels 1-4) are shown at once. Each track gets two
/// pad rows, giving up to 32 steps: steps 1-16 on the upper row and 17-32 on the lower row (bright = gate event,
/// dim = empty step inside the track's length, dark = beyond the length). The track's playhead is the white step.
/// Each track has its own independent length (the NoteRow's independent loop length), so the four tracks drift
/// against each other as polyrhythms.
///
/// Controls (see the mapping constants in euclidean_view.cpp to change them):
///   upper black encoder (horizontal) -> gate 1    lower black encoder (vertical) -> gate 2
///   upper gold knob                  -> gate 3    lower gold knob                -> gate 4
///   turning an encoder sets that track's number of hits, spread evenly (a Euclidean rhythm)
///   hold SHIFT while turning it -> that track's length in steps
///   pattern pads    -> toggle a gate event on that step
///   BACK            -> return to the Kit clip view
///
/// Enter from any instrument clip with SHIFT + CV (a clip that isn't a Kit is turned into one) and leave with BACK or
/// SHIFT + CV again. Any of the four gate rows the Kit doesn't have yet are created automatically on entry, so no
/// manual gate setup is needed. Tapping a pattern pad adds or removes a gate event on that step.
class EuclideanView final : public ClipView {
public:
	EuclideanView() = default;

	static constexpr int32_t kNumGateTracks = 4;
	/// Longest track the view can show: each track has two rows of one step per pad column
	static constexpr int32_t kMaxSteps = 2 * kDisplayWidth;

	// UI plumbing
	bool opened() override;
	void focusRegained() override;
	UIType getUIType() override { return UIType::EUCLIDEAN; }
	UIModControllableContext getUIModControllableContext() override { return UIModControllableContext::CLIP; }

	// rendering
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;
	void graphicsRoutine() override;

	// input
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override;
	void selectEncoderAction(int8_t offset) override {}

private:
	/// Makes sure the current Kit clip has a gate row for each of gate outputs 1-4, creating any that are missing so
	/// the view works without any manual setup. Existing rows are left alone.
	void ensureGateRows(InstrumentClip* clip);

	/// Finds the Kit row driving gate channel `channel` (0-3). Returns nullptr if the current clip is not a Kit or has
	/// no GateDrum row for that channel. On success `noteRowIndex` is the row's index within the clip.
	NoteRow* findGateRow(InstrumentClip* clip, int32_t channel, int32_t& noteRowIndex);

	/// Number of steps the row currently spans at the current zoom, capped to kMaxSteps for display
	int32_t getNumSteps(ModelStackWithNoteRow* modelStack, uint32_t stepWidth);

	/// Changes one track's length by `offset` steps, with undo support
	void editTrackLength(int32_t channel, int32_t offset);

	/// Changes one track's number of gate events by `offset` and respaces them as evenly as possible across the track
	/// (a Euclidean rhythm), with undo support
	void editTrackHits(int32_t channel, int32_t offset);

	/// An encoder turn for a track: edits its length while SHIFT is held, otherwise its hits
	void encoderTurned(int32_t channel, int32_t offset);

	void showHitsPopup(int32_t channel, int32_t numHits, int32_t numSteps);

	/// Step the track's playhead is on, or -1 if playback isn't running for this clip
	int32_t getPlayheadStep(InstrumentClip* clip, int32_t channel);

	void showLengthPopup(int32_t channel, int32_t numSteps);
	void renderTrack(RGB image[][kDisplayWidth + kSideBarWidth], int32_t channel, uint32_t whichRows);
	void refreshAll() { uiNeedsRendering(this); }

	/// Step each track's playhead was last drawn at, so graphicsRoutine only redraws on a change
	int32_t lastDrawnPlayheadStep_[kNumGateTracks] = {-1, -1, -1, -1};
};

extern EuclideanView euclideanView;
