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

#include <cmath>
#include <cstdint>

namespace deluge::quantiser {

/// Snaps a CV DAC code to the nearest note of a scale.
///
/// 0 V is a C, so a code of N * codePerSemitone is note N semitones above C. `scaleBits` has bit n set when the note n
/// semitones above the root is allowed (the song's scale, bit 0 is the root); an empty scale means every semitone.
/// `rootPitchClass` is 0 to 11 (C = 0). Ties go to the lower note.
inline int32_t snapCode(int32_t code, float codePerSemitone, int32_t rootPitchClass, uint16_t scaleBits) {
	if (codePerSemitone <= 0.0f) {
		return code;
	}
	float position = static_cast<float>(code) / codePerSemitone;
	int32_t base = static_cast<int32_t>(std::floor(position));
	int32_t best = -1;
	float bestDistance = 1.0e9f;
	for (int32_t candidate = base - 7; candidate <= base + 8; candidate++) {
		if (candidate < 0) {
			continue;
		}
		int32_t interval = (((candidate - rootPitchClass) % 12) + 12) % 12;
		if (scaleBits != 0 && !((scaleBits >> interval) & 1)) {
			continue;
		}
		float distance = std::fabs(static_cast<float>(candidate) - position);
		if (distance < bestDistance - 1.0e-4f) {
			bestDistance = distance;
			best = candidate;
		}
	}
	if (best < 0) {
		return code;
	}
	return static_cast<int32_t>(std::lround(static_cast<float>(best) * codePerSemitone));
}

} // namespace deluge::quantiser
