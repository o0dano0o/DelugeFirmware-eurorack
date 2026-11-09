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

#include <algorithm>
#include <cstdint>

namespace deluge::euclid {

/// How one Euclidean gate track gets nudged by another (or by itself)
struct EvolveRule {
	enum Target : uint8_t { HITS, ROTATE };
	enum Direction : uint8_t { UP, DOWN, PINGPONG, RANDOM };

	int8_t source = -1;       ///< gate track (0-3) whose hits drive this, or -1 for none
	uint8_t every = 4;        ///< change after this many hits of the source (1-16)
	Target target = HITS;     ///< what changes: the number of hits or the rotation
	Direction direction = UP; ///< which way it moves
	uint8_t amount = 1;       ///< how far it moves each time (1-4)

	bool active() const { return source >= 0; }

	/// Packed into one number for saving
	int32_t pack() const {
		return (source + 1) | (every << 3) | (target << 8) | (direction << 9) | (amount << 11);
	}
	static EvolveRule unpack(int32_t v) {
		EvolveRule r;
		r.source = static_cast<int8_t>((v & 7) - 1);
		r.every = static_cast<uint8_t>(std::clamp<int32_t>((v >> 3) & 31, 1, 16));
		r.target = ((v >> 8) & 1) ? ROTATE : HITS;
		r.direction = static_cast<Direction>((v >> 9) & 3);
		r.amount = static_cast<uint8_t>(std::clamp<int32_t>((v >> 11) & 7, 1, 4));
		if (r.source > 3) {
			r.source = -1;
		}
		return r;
	}
};

/// The next value of a setting that lives in lo..hi. `sign` is the ping-pong direction (+1 or -1) and is updated;
/// `random` is any random number. Up and down wrap around, ping-pong bounces off the ends, random walks one way or the
/// other and stops at the ends.
inline int32_t evolveValue(const EvolveRule& rule, int32_t current, int32_t lo, int32_t hi, int8_t& sign,
                           uint32_t random) {
	if (hi <= lo) {
		return lo;
	}
	int32_t size = hi - lo + 1;
	int32_t step = rule.amount;
	current = std::clamp(current, lo, hi);
	auto wrap = [&](int32_t v) { return lo + ((((v - lo) % size) + size) % size); };
	switch (rule.direction) {
	case EvolveRule::UP:
		return wrap(current + step);
	case EvolveRule::DOWN:
		return wrap(current - step);
	case EvolveRule::PINGPONG: {
		int32_t next = current + sign * step;
		if (next > hi) {
			sign = -1;
			next = std::max(lo, hi - (next - hi));
		}
		else if (next < lo) {
			sign = 1;
			next = std::min(hi, lo + (lo - next));
		}
		return next;
	}
	case EvolveRule::RANDOM:
	default:
		return std::clamp(current + ((random & 1) ? step : -step), lo, hi);
	}
}

} // namespace deluge::euclid
