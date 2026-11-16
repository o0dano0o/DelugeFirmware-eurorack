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

#include "processing/engines/crossmod_engine.h"
#include <cmath>

namespace deluge::crossmod {

namespace {
constexpr float kEditTolerance = 0.003f;

float clamp01(float v) {
	if (!(v >= 0.0f)) { // also catches not-a-number
		return 0.0f;
	}
	return v > 1.0f ? 1.0f : v;
}

int32_t clampSteps(int32_t v) {
	return v < -kMaxDepthSteps ? -kMaxDepthSteps : (v > kMaxDepthSteps ? kMaxDepthSteps : v);
}
} // namespace

void CrossmodEngine::reset() {
	*this = CrossmodEngine();
}

void CrossmodEngine::setRoute(int32_t index, int32_t source, int32_t target, int32_t depth) {
	if (index < 0 || index >= kNumRoutes) {
		return;
	}
	setSource(index, source);
	setTarget(index, target);
	setDepth(index, depth);
}

void CrossmodEngine::setSource(int32_t index, int32_t source) {
	if (index < 0 || index >= kNumRoutes) {
		return;
	}
	routes_[index].source = static_cast<int8_t>((source >= 0 && source < kMaxSources) ? source : -1);
}

void CrossmodEngine::setTarget(int32_t index, int32_t target) {
	if (index < 0 || index >= kNumRoutes) {
		return;
	}
	routes_[index].target = static_cast<int8_t>((target >= 0 && target < kMaxTargets) ? target : -1);
}

void CrossmodEngine::setDepth(int32_t index, int32_t depth) {
	if (index < 0 || index >= kNumRoutes) {
		return;
	}
	routes_[index].depth = static_cast<int8_t>(clampSteps(depth));
}

void CrossmodEngine::clearRoute(int32_t index) {
	if (index < 0 || index >= kNumRoutes) {
		return;
	}
	routes_[index] = Route();
	shift_[index] = 0.0f;
}

const Route& CrossmodEngine::route(int32_t index) const {
	static const Route none;
	return (index >= 0 && index < kNumRoutes) ? routes_[index] : none;
}

bool CrossmodEngine::anyActive() const {
	for (const Route& r : routes_) {
		if (r.source >= 0 && r.target >= 0 && r.depth != 0) {
			return true;
		}
	}
	return false;
}

bool CrossmodEngine::anyUsed() const {
	for (const Route& r : routes_) {
		if (r.source >= 0 || r.target >= 0) {
			return true;
		}
	}
	return false;
}

void CrossmodEngine::update(const float sources[kMaxSources], const float current[kMaxTargets],
                            const bool available[kMaxTargets], float out[kMaxTargets], bool write[kMaxTargets]) {
	float total[kMaxTargets] = {};
	bool used[kMaxTargets] = {};

	for (int32_t i = 0; i < kNumRoutes; i++) {
		const Route& r = routes_[i];
		shift_[i] = 0.0f;
		if (r.source < 0 || r.target < 0 || r.depth == 0 || !available[r.target]) {
			continue;
		}
		float s = std::isnan(sources[r.source]) ? 0.5f : clamp01(sources[r.source]);
		sourceSeen_[i] = s;
		float shift = (static_cast<float>(r.depth) / kMaxDepthSteps) * (s * 2.0f - 1.0f);
		shift_[i] = shift;
		total[r.target] += shift;
		used[r.target] = true;
	}

	for (int32_t t = 0; t < kMaxTargets; t++) {
		write[t] = false;
		out[t] = current[t];
		if (!used[t]) {
			if (tracked_[t]) {
				// the last route went away: hand the setting back, unless the target stopped or was edited
				if (available[t] && std::fabs(clamp01(current[t]) - written_[t]) <= kEditTolerance) {
					out[t] = base_[t];
					write[t] = true;
				}
				tracked_[t] = false;
			}
			continue;
		}
		float now = clamp01(current[t]);
		if (!tracked_[t] || std::fabs(now - written_[t]) > kEditTolerance) {
			base_[t] = now; // first time, or the setting was changed by hand
			tracked_[t] = true;
		}
		float value = clamp01(base_[t] + total[t]);
		written_[t] = value;
		out[t] = value;
		write[t] = true;
	}
}

float CrossmodEngine::lastShift(int32_t index) const {
	return (index >= 0 && index < kNumRoutes) ? shift_[index] : 0.0f;
}

float CrossmodEngine::lastSource(int32_t index) const {
	return (index >= 0 && index < kNumRoutes) ? sourceSeen_[index] : 0.0f;
}

} // namespace deluge::crossmod
