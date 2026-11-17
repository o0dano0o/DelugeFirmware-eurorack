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

#include "processing/engines/sandpile_engine.h"
#include <cmath>

namespace deluge::sandpile {

namespace {
constexpr uint32_t kSeed = 0x9E3779B9u;
constexpr float kPulseSeconds = 0.010f;
constexpr float kFlashSeconds = 0.08f;
constexpr float kVoltCode = 6552.0f;
constexpr float kMaxCode = 65520.0f;
constexpr float kMaxStepSeconds = 0.01f;
constexpr int32_t kWarmupDrops = 300;
constexpr float kSizeScale = 6.0f; ///< log2(1 + size) of this is full scale (63 topples)

float clamp(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

int32_t index(int32_t x, int32_t y) {
	return y * kWidth + x;
}
} // namespace

uint32_t SandpileEngine::nextRandom() {
	rng_ ^= rng_ << 13;
	rng_ ^= rng_ >> 17;
	rng_ ^= rng_ << 5;
	return rng_;
}

void SandpileEngine::clear() {
	for (int32_t i = 0; i < kNumCells; i++) {
		cells_[i] = 0;
		flash_[i] = 0.0f;
	}
	for (float& p : pulse_) {
		p = 0.0f;
	}
	sliding_ = false;
	size_ = 0;
	lastSize_ = 0;
	dropTimer_ = 0.0f;
	roundTimer_ = 0.0f;
}

void SandpileEngine::reset() {
	rng_ = kSeed;
	dropped_ = 0;
	lost_ = 0;
	topples_ = 0;
	avalanches_ = 0;
	clear();
	DropMode mode = params_.dropMode;
	params_.dropMode = DropMode::RANDOM;
	for (int32_t i = 0; i < kWarmupDrops; i++) {
		drop();
		relaxNow();
	}
	params_.dropMode = mode;
	// the warm-up isn't part of the run
	dropped_ = 0;
	lost_ = 0;
	topples_ = 0;
	avalanches_ = 0;
	lastSize_ = 0;
	size_ = 0;
	sliding_ = false;
	for (float& f : flash_) {
		f = 0.0f;
	}
	for (float& p : pulse_) {
		p = 0.0f;
	}
}

void SandpileEngine::setParams(const Params& p) {
	params_ = p;
	params_.dropRate = clamp(p.dropRate, 0.25f, 20.0f);
	params_.bigSize = static_cast<int32_t>(clamp(static_cast<float>(p.bigSize), 3.0f, 60.0f));
	params_.toppleRate = clamp(p.toppleRate, 5.0f, 100.0f);
	params_.rangeVolts = clamp(p.rangeVolts, 1.0f, 10.0f);
}

void SandpileEngine::setDropRate(float v) {
	params_.dropRate = clamp(v, 0.25f, 20.0f);
}
void SandpileEngine::setDropMode(DropMode m) {
	params_.dropMode = m;
}
void SandpileEngine::setBigSize(int32_t n) {
	params_.bigSize = static_cast<int32_t>(clamp(static_cast<float>(n), 3.0f, 60.0f));
}
void SandpileEngine::setToppleRate(float v) {
	params_.toppleRate = clamp(v, 5.0f, 100.0f);
}
void SandpileEngine::setRange(float volts) {
	params_.rangeVolts = clamp(volts, 1.0f, 10.0f);
}
void SandpileEngine::setTimeMode(TimeMode m) {
	params_.timeMode = m;
}

bool SandpileEngine::addGrain(int32_t x, int32_t y) {
	if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
		return false;
	}
	cells_[index(x, y)]++;
	dropped_++;
	return true;
}

bool SandpileEngine::removeGrain(int32_t x, int32_t y) {
	if (x < 0 || x >= kWidth || y < 0 || y >= kHeight || cells_[index(x, y)] == 0) {
		return false;
	}
	cells_[index(x, y)]--;
	return true;
}

void SandpileEngine::setCell(int32_t x, int32_t y, int32_t grains) {
	if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
		return;
	}
	cells_[index(x, y)] = static_cast<uint8_t>(grains < 0 ? 0 : (grains > 255 ? 255 : grains));
}

bool SandpileEngine::anyUnstable() const {
	for (int32_t i = 0; i < kNumCells; i++) {
		if (cells_[i] >= kLimit) {
			return true;
		}
	}
	return false;
}

int32_t SandpileEngine::relaxNow() {
	int32_t total = 0;
	while (anyUnstable()) {
		uint32_t before = topples_;
		topplingRound();
		total += static_cast<int32_t>(topples_ - before);
	}
	return total;
}

/// One round: every cell that is over the limit topples at once
void SandpileEngine::topplingRound() {
	uint8_t over[kNumCells];
	for (int32_t i = 0; i < kNumCells; i++) {
		over[i] = cells_[i] >= kLimit ? static_cast<uint8_t>(cells_[i] / kLimit) : 0;
	}
	for (int32_t y = 0; y < kHeight; y++) {
		for (int32_t x = 0; x < kWidth; x++) {
			uint8_t n = over[index(x, y)];
			if (!n) {
				continue;
			}
			cells_[index(x, y)] = static_cast<uint8_t>(cells_[index(x, y)] - n * kLimit);
			flash_[index(x, y)] = kFlashSeconds;
			topples_ += n;
			size_ += n;
			const int32_t dx[4] = {1, -1, 0, 0};
			const int32_t dy[4] = {0, 0, 1, -1};
			for (int32_t d = 0; d < 4; d++) {
				int32_t nx = x + dx[d];
				int32_t ny = y + dy[d];
				if (nx < 0 || nx >= kWidth || ny < 0 || ny >= kHeight) {
					lost_ += n;
				}
				else {
					cells_[index(nx, ny)] = static_cast<uint8_t>(cells_[index(nx, ny)] + n);
				}
			}
		}
	}
}

void SandpileEngine::firePulse(int32_t gate) {
	pulse_[gate] = kPulseSeconds;
}

void SandpileEngine::drop() {
	int32_t x;
	int32_t y;
	if (params_.dropMode == DropMode::CENTRE) {
		x = kWidth / 2;
		y = kHeight / 2;
	}
	else {
		uint32_t r = nextRandom() >> 8;
		x = static_cast<int32_t>(r % kWidth);
		y = static_cast<int32_t>((r / kWidth) % kHeight);
	}
	addGrain(x, y);
}

void SandpileEngine::step(float dt, float bpm) {
	float realDt = dt;
	if (params_.timeMode == TimeMode::TEMPO) {
		dt *= clamp(bpm, 1.0f, 999.0f) / 120.0f;
	}
	dt = dt > kMaxStepSeconds ? kMaxStepSeconds : dt;

	dropTimer_ += dt;
	const float dropEvery = 1.0f / params_.dropRate;
	while (dropTimer_ >= dropEvery) {
		dropTimer_ -= dropEvery;
		drop();
		firePulse(0);
	}
	// a drop, or a grain added by hand, can leave a cell over the limit
	if (!sliding_ && anyUnstable()) {
		sliding_ = true;
		size_ = 0;
		roundTimer_ = 0.0f;
		firePulse(1);
	}

	if (sliding_) {
		roundTimer_ += dt;
		const float roundEvery = 1.0f / params_.toppleRate;
		while (sliding_ && roundTimer_ >= roundEvery) {
			roundTimer_ -= roundEvery;
			topplingRound();
			firePulse(2);
			if (!anyUnstable()) {
				sliding_ = false;
				lastSize_ = size_;
				avalanches_++;
				if (size_ >= params_.bigSize) {
					firePulse(3);
				}
			}
		}
	}

	for (float& f : flash_) {
		if (f > 0.0f) {
			f -= realDt;
			if (f < 0.0f) {
				f = 0.0f;
			}
		}
	}
	for (float& p : pulse_) {
		if (p > 0.0f) {
			p -= realDt;
			if (p < 0.0f) {
				p = 0.0f;
			}
		}
	}
}

int32_t SandpileEngine::getCell(int32_t x, int32_t y) const {
	return (x >= 0 && x < kWidth && y >= 0 && y < kHeight) ? cells_[index(x, y)] : 0;
}

float SandpileEngine::getFlash(int32_t x, int32_t y) const {
	return (x >= 0 && x < kWidth && y >= 0 && y < kHeight) ? flash_[index(x, y)] : 0.0f;
}

int32_t SandpileEngine::getTotalGrains() const {
	int32_t sum = 0;
	for (int32_t i = 0; i < kNumCells; i++) {
		sum += cells_[i];
	}
	return sum;
}

float SandpileEngine::getVolts(int32_t cv) const {
	if (cv == 0) {
		int32_t size = sliding_ ? size_ : lastSize_;
		float norm = std::log2(1.0f + static_cast<float>(size)) / kSizeScale;
		return clamp(norm, 0.0f, 1.0f) * params_.rangeVolts;
	}
	if (cv == 1) {
		// Grains per cell from 1.5 (0 V) to 2.5 (full range). A settled pile sits near 2, so one grain or a lost slide
		// moves this by a visible amount; a straight 0 to 3 scale would barely twitch.
		float avg = static_cast<float>(getTotalGrains()) / static_cast<float>(kNumCells);
		return clamp(avg - 1.5f, 0.0f, 1.0f) * params_.rangeVolts;
	}
	return 0.0f;
}

uint16_t SandpileEngine::getCVCode(int32_t cv) const {
	float code = clamp(getVolts(cv) * kVoltCode + 0.5f, 0.0f, kMaxCode);
	return static_cast<uint16_t>(code);
}

uint8_t SandpileEngine::getGateLevels() const {
	uint8_t levels = 0;
	for (int32_t i = 0; i < kNumGates; i++) {
		if (pulse_[i] > 0.0f) {
			levels |= static_cast<uint8_t>(1 << i);
		}
	}
	return levels;
}

} // namespace deluge::sandpile
