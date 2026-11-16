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

/// Cross-modulation: lets one generator's output move another generator's setting.
///
/// This is only the routing maths. Everything is normalised: a source is a number from 0 to 1 and a target is a
/// setting from 0 (its lowest) to 1 (its highest). A route has a source, a target and a depth. The route moves the
/// target by depth x (source x 2 - 1), so a depth of 1 sweeps the whole range as the source goes from 0 to 1, and a
/// source sitting at half does nothing. Several routes on one target add up. The result is always held between 0
/// and 1, so a loop of routes cannot run away.
///
/// The target's own value (what you set on its view) is its base. If you change the setting while a route is
/// working on it, the new value becomes the base. When the last route on a target goes away the base is put back.
namespace deluge::crossmod {

constexpr int32_t kNumRoutes = 4;
constexpr int32_t kMaxSources = 24;
constexpr int32_t kMaxTargets = 24;
constexpr int32_t kMaxDepthSteps = 20; ///< depth is stored in steps of 0.05, -20 to 20 (-1 to 1)

struct Route {
	int8_t source = -1; ///< -1 means none
	int8_t target = -1;
	int8_t depth = 0;
};

class CrossmodEngine {
public:
	void reset();

	void setRoute(int32_t index, int32_t source, int32_t target, int32_t depth);
	void setSource(int32_t index, int32_t source);
	void setTarget(int32_t index, int32_t target);
	void setDepth(int32_t index, int32_t depth);
	void clearRoute(int32_t index);
	const Route& route(int32_t index) const;
	/// True if any route has both a source and a target and a depth other than zero
	bool anyActive() const;
	/// True if any route has a source or a target chosen
	bool anyUsed() const;

	/// Runs one update. sources and current are normalised (anything else is held to 0 to 1; not-a-number counts as
	/// 0.5 for a source). available says whether a target's generator is running: routes on a target that is not
	/// available are ignored. For each target where write[t] comes back true, set that setting to out[t].
	void update(const float sources[kMaxSources], const float current[kMaxTargets], const bool available[kMaxTargets],
	            float out[kMaxTargets], bool write[kMaxTargets]);

	/// What the route last added to its target (-1 to 1), for displays
	float lastShift(int32_t index) const;
	/// The source value the route last read, 0 to 1
	float lastSource(int32_t index) const;

private:
	Route routes_[kNumRoutes];
	bool tracked_[kMaxTargets] = {};
	float base_[kMaxTargets] = {};
	float written_[kMaxTargets] = {};
	float shift_[kNumRoutes] = {};
	float sourceSeen_[kNumRoutes] = {};
};

} // namespace deluge::crossmod
