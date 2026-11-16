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

#include "processing/engines/billiards_engine.h"
#include <cmath>

namespace deluge::billiards {

namespace {
constexpr uint32_t kSeed = 0x1B873593u;
constexpr float kPulseSeconds = 0.010f;
constexpr float kVoltCode = 6552.0f;
constexpr float kMaxCode = 65520.0f;
constexpr float kMinSpeed = 0.5f;
constexpr float kMaxSpeed = 30.0f;
constexpr float kMaxGravity = 30.0f;
constexpr float kMinBounce = 0.3f;
constexpr float kMaxStepSeconds = 0.01f;
constexpr float kBallDiameter = 0.8f;
constexpr float kStillSpeed = 0.35f; ///< below this a ball is launched again
constexpr float kEdge = 0.0001f;

float clamp(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

uint32_t BilliardsEngine::nextRandom() {
	rng_ ^= rng_ << 13;
	rng_ ^= rng_ >> 17;
	rng_ ^= rng_ << 5;
	return rng_;
}

float BilliardsEngine::random01() {
	return static_cast<float>(nextRandom() >> 8) * (1.0f / 16777216.0f);
}

void BilliardsEngine::reset() {
	rng_ = kSeed;
	totalHits_ = 0;
	for (int32_t i = 0; i < kMaxBalls; i++) {
		float angle = 0.5f + 1.7f * static_cast<float>(i);
		x_[i] = 2.5f + 3.7f * static_cast<float>(i);
		y_[i] = 2.0f + 1.5f * static_cast<float>(i);
		vx_[i] = params_.speed * std::cos(angle);
		vy_[i] = params_.speed * std::sin(angle);
		hits_[i] = 0;
	}
	for (float& p : pulse_) {
		p = 0.0f;
	}
}

void BilliardsEngine::setParams(const Params& p) {
	params_ = p;
	params_.numBalls = static_cast<int32_t>(clamp(static_cast<float>(p.numBalls), 1.0f, static_cast<float>(kMaxBalls)));
	params_.speed = clamp(p.speed, kMinSpeed, kMaxSpeed);
	params_.gravity = clamp(p.gravity, 0.0f, kMaxGravity);
	params_.bounce = clamp(p.bounce, kMinBounce, 1.0f);
	params_.cvBall = static_cast<int32_t>(clamp(static_cast<float>(p.cvBall), 0.0f, static_cast<float>(kMaxBalls - 1)));
	params_.rangeVolts = clamp(p.rangeVolts, 1.0f, 10.0f);
}

void BilliardsEngine::setNumBalls(int32_t n) {
	Params p = params_;
	p.numBalls = n;
	setParams(p);
}

void BilliardsEngine::setSpeed(float v) {
	float old = params_.speed;
	params_.speed = clamp(v, kMinSpeed, kMaxSpeed);
	float scale = params_.speed / old;
	for (int32_t i = 0; i < kMaxBalls; i++) {
		vx_[i] *= scale;
		vy_[i] *= scale;
	}
}

void BilliardsEngine::setGravity(float v) {
	params_.gravity = clamp(v, 0.0f, kMaxGravity);
}

void BilliardsEngine::setBounce(float v) {
	params_.bounce = clamp(v, kMinBounce, 1.0f);
}

void BilliardsEngine::setCvBall(int32_t ball) {
	params_.cvBall = static_cast<int32_t>(clamp(static_cast<float>(ball), 0.0f, static_cast<float>(kMaxBalls - 1)));
}

void BilliardsEngine::setGateMode(GateMode m) {
	params_.gateMode = m;
}

void BilliardsEngine::setRange(float volts) {
	params_.rangeVolts = clamp(volts, 1.0f, 10.0f);
}

void BilliardsEngine::setTimeMode(TimeMode m) {
	params_.timeMode = m;
}

bool BilliardsEngine::hasBumper(int32_t x, int32_t y) const {
	if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
		return false;
	}
	return (params_.bumpers[y] >> x) & 1;
}

void BilliardsEngine::toggleBumper(int32_t x, int32_t y) {
	if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
		return;
	}
	params_.bumpers[y] ^= static_cast<uint16_t>(1u << x);
}

void BilliardsEngine::clearBumpers() {
	for (uint16_t& b : params_.bumpers) {
		b = 0;
	}
}

void BilliardsEngine::launch(int32_t ball) {
	float angle = random01() * 6.2831853f;
	vx_[ball] = params_.speed * std::cos(angle);
	vy_[ball] = params_.speed * std::sin(angle);
}

void BilliardsEngine::relaunch() {
	for (int32_t i = 0; i < kMaxBalls; i++) {
		launch(i);
	}
}

void BilliardsEngine::setBall(int32_t ball, float x, float y, float vx, float vy) {
	if (ball < 0 || ball >= kMaxBalls) {
		return;
	}
	x_[ball] = clamp(x, 0.0f, kWidth - kEdge);
	y_[ball] = clamp(y, 0.0f, kHeight - kEdge);
	vx_[ball] = vx;
	vy_[ball] = vy;
}

bool BilliardsEngine::bumperAt(float x, float y) const {
	return hasBumper(static_cast<int32_t>(std::floor(x)), static_cast<int32_t>(std::floor(y)));
}

void BilliardsEngine::fire(int32_t ball, int32_t wallGate) {
	hits_[ball]++;
	totalHits_++;
	int32_t gate = params_.gateMode == GateMode::BALLS ? ball : wallGate;
	if (gate >= 0 && gate < kNumGates) {
		pulse_[gate] = kPulseSeconds;
	}
}

void BilliardsEngine::moveBall(int32_t i, float dt) {
	const float e = params_.bounce;
	vy_[i] -= params_.gravity * dt;
	float ox = x_[i];
	float oy = y_[i];
	float nx = ox + vx_[i] * dt;
	float ny = oy + vy_[i] * dt;

	// bumpers: only when a ball enters one from outside
	if (bumperAt(nx, ny) && !bumperAt(ox, oy)) {
		bool xBlocked = bumperAt(nx, oy);
		bool yBlocked = bumperAt(ox, ny);
		if (xBlocked || !yBlocked) {
			vx_[i] = -vx_[i] * e;
			nx = ox;
		}
		if (yBlocked || !xBlocked) {
			vy_[i] = -vy_[i] * e;
			ny = oy;
		}
		fire(i, 3);
	}

	if (nx < 0.0f) {
		nx = -nx;
		vx_[i] = -vx_[i] * e;
		fire(i, 0);
	}
	else if (nx >= kWidth) {
		nx = 2.0f * kWidth - nx;
		vx_[i] = -vx_[i] * e;
		fire(i, 1);
	}
	if (ny < 0.0f) {
		ny = -ny;
		vy_[i] = -vy_[i] * e;
		fire(i, 2);
	}
	else if (ny >= kHeight) {
		ny = 2.0f * kHeight - ny;
		vy_[i] = -vy_[i] * e;
		fire(i, 2);
	}
	x_[i] = clamp(nx, 0.0f, kWidth - kEdge);
	y_[i] = clamp(ny, 0.0f, kHeight - kEdge);

	// a ball that has run out of energy is launched again
	float speed2 = vx_[i] * vx_[i] + vy_[i] * vy_[i];
	if (speed2 < kStillSpeed * kStillSpeed && (params_.gravity == 0.0f || y_[i] < 0.3f)) {
		launch(i);
	}
}

void BilliardsEngine::collideBalls() {
	for (int32_t a = 0; a < params_.numBalls; a++) {
		for (int32_t b = a + 1; b < params_.numBalls; b++) {
			float dx = x_[b] - x_[a];
			float dy = y_[b] - y_[a];
			float d2 = dx * dx + dy * dy;
			if (d2 >= kBallDiameter * kBallDiameter || d2 <= 1e-10f) {
				continue;
			}
			float d = std::sqrt(d2);
			float nx = dx / d;
			float ny = dy / d;
			float rel = (vx_[b] - vx_[a]) * nx + (vy_[b] - vy_[a]) * ny;
			if (rel >= 0.0f) {
				continue; // already moving apart
			}
			// equal masses: swap the speeds along the line between them
			vx_[a] += rel * nx;
			vy_[a] += rel * ny;
			vx_[b] -= rel * nx;
			vy_[b] -= rel * ny;
			float push = (kBallDiameter - d) * 0.5f;
			x_[a] = clamp(x_[a] - nx * push, 0.0f, kWidth - kEdge);
			y_[a] = clamp(y_[a] - ny * push, 0.0f, kHeight - kEdge);
			x_[b] = clamp(x_[b] + nx * push, 0.0f, kWidth - kEdge);
			y_[b] = clamp(y_[b] + ny * push, 0.0f, kHeight - kEdge);
			fire(a, 3);
			fire(b, 3);
		}
	}
}

void BilliardsEngine::step(float dt, float bpm) {
	float realDt = dt;
	if (params_.timeMode == TimeMode::TEMPO) {
		dt *= clamp(bpm, 1.0f, 999.0f) / 120.0f;
	}
	dt = dt > kMaxStepSeconds ? kMaxStepSeconds : dt;

	for (int32_t i = 0; i < params_.numBalls; i++) {
		moveBall(i, dt);
	}
	collideBalls();

	for (float& p : pulse_) {
		if (p > 0.0f) {
			p -= realDt;
			if (p < 0.0f) {
				p = 0.0f;
			}
		}
	}
}

float BilliardsEngine::getX(int32_t b) const {
	return (b >= 0 && b < kMaxBalls) ? x_[b] : 0.0f;
}
float BilliardsEngine::getY(int32_t b) const {
	return (b >= 0 && b < kMaxBalls) ? y_[b] : 0.0f;
}
float BilliardsEngine::getVx(int32_t b) const {
	return (b >= 0 && b < kMaxBalls) ? vx_[b] : 0.0f;
}
float BilliardsEngine::getVy(int32_t b) const {
	return (b >= 0 && b < kMaxBalls) ? vy_[b] : 0.0f;
}
uint32_t BilliardsEngine::getHitCount(int32_t b) const {
	return (b >= 0 && b < kMaxBalls) ? hits_[b] : 0;
}

float BilliardsEngine::getVolts(int32_t cv) const {
	int32_t b = params_.cvBall < params_.numBalls ? params_.cvBall : 0;
	if (cv == 0) {
		return x_[b] / kWidth * params_.rangeVolts;
	}
	if (cv == 1) {
		return y_[b] / kHeight * params_.rangeVolts;
	}
	return 0.0f;
}

uint16_t BilliardsEngine::getCVCode(int32_t cv) const {
	float code = clamp(getVolts(cv) * kVoltCode + 0.5f, 0.0f, kMaxCode);
	return static_cast<uint16_t>(code);
}

uint8_t BilliardsEngine::getGateLevels() const {
	uint8_t levels = 0;
	for (int32_t i = 0; i < kNumGates; i++) {
		if (pulse_[i] > 0.0f) {
			levels |= static_cast<uint8_t>(1 << i);
		}
	}
	return levels;
}

} // namespace deluge::billiards
