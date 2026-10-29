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
/// pad rows: the upper one shows the pattern (bright = gate event, dim = empty step inside the track's length, dark =
/// beyond the length) and the lower one shows that track's own playhead. Each track has its own independent length
/// (the NoteRow's independent loop length), so the four tracks drift against each other as polyrhythms.
///
/// Controls (see the mapping constants in euclidean_view.cpp to change them):
///   upper gold knob -> gate 1 length     lower gold knob      -> gate 2 length
///   horizontal <>   -> gate 3 length     vertical (up/down)   -> gate 4 length
///   BACK            -> return to the Kit clip view
///
/// Enter from a Kit clip with SHIFT + KIT.
class EuclideanView final : public ClipView {
public:
	EuclideanView() = default;

	static constexpr int32_t kNumGateTracks = 4;
	/// Longest track the view can show: one step per pad column
	static constexpr int32_t kMaxSteps = kDisplayWidth;

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
	/// Finds the Kit row driving gate channel `channel` (0-3). Returns nullptr if the current clip is not a Kit or has
	/// no GateDrum row for that channel. On success `noteRowIndex` is the row's index within the clip.
	NoteRow* findGateRow(InstrumentClip* clip, int32_t channel, int32_t& noteRowIndex);

	/// Number of steps the row currently spans at the current zoom, capped to kMaxSteps for display
	int32_t getNumSteps(ModelStackWithNoteRow* modelStack, uint32_t stepWidth);

	/// Changes one track's length by `offset` steps, with undo support
	void editTrackLength(int32_t channel, int32_t offset);

	/// Step the track's playhead is on, or -1 if playback isn't running for this clip
	int32_t getPlayheadStep(InstrumentClip* clip, int32_t channel);

	void showLengthPopup(int32_t channel, int32_t numSteps);
	void renderTrackRow(RGB* image, int32_t channel, bool patternRow);
	void refreshAll() { uiNeedsRendering(this); }

	/// Step each track's playhead was last drawn at, so graphicsRoutine only redraws on a change
	int32_t lastDrawnPlayheadStep_[kNumGateTracks] = {-1, -1, -1, -1};
};

extern EuclideanView euclideanView;
