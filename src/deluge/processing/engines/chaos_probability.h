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

#include <cstdint>

/// Chaos-driven hit probability for gates that are still playing their Euclidean pattern. Pure functions, no
/// hardware, so they can be tested on the host.
namespace deluge::chaos::probability {

/// Probability that a hit passes. `x` is a sloth's normalised x (-1 to 1) and `depth` is 0 to 1.
/// depth 0: every hit passes. depth 1: the probability follows the chaos from 0 (x = -1) to 1 (x = 1).
inline float passProbability(float x, float depth) {
	if (x < -1.0f) {
		x = -1.0f;
	}
	else if (x > 1.0f) {
		x = 1.0f;
	}
	if (depth < 0.0f) {
		depth = 0.0f;
	}
	else if (depth > 1.0f) {
		depth = 1.0f;
	}
	float chaos01 = 0.5f * (x + 1.0f);
	return 1.0f - depth * (1.0f - chaos01);
}

/// Seeded xorshift32. The runner reseeds it whenever the chaos restarts on play, so a given pattern drops the same
/// hits on every play (within one build).
struct Roller {
	static constexpr uint32_t kSeed = 0x2545F491u;
	uint32_t state = kSeed;

	void reset() { state = kSeed; }

	/// Uniform in [0, 1)
	float next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
	}

	/// True when the hit should pass
	bool roll(float p) { return next() < p; }
};

} // namespace deluge::chaos::probability
