// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <ddk/debug.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/pdev.h>

namespace audio {
namespace mt8167 {

class Tas5782 {
public:
    static std::unique_ptr<Tas5782> Create(pdev_protocol_t& pdev, uint32_t i2c_index);

    explicit Tas5782(const ddk::I2cChannel& i2c)
        : i2c_(i2c) {}

    bool ValidGain(float gain);
    zx_status_t SetGain(float gain);
    zx_status_t Init();
    zx_status_t Reset();
    zx_status_t Standby();
    zx_status_t ExitStandby();
    float GetGain() const { return current_gain_; }
    float GetMinGain() { return kMinGain; }
    float GetMaxGain() { return kMaxGain; }
    float GetGainStep() const { return kGainStep; }

private:
    static constexpr float kMaxGain = 24.0;
    static constexpr float kMinGain = -103.0;
    static constexpr float kGainStep = 0.5;

    zx_status_t WriteReg(uint8_t reg, uint8_t value);

    zx_status_t SetStandby(bool stdby);

    ddk::I2cChannel i2c_;

    float current_gain_ = 0;
};
} // namespace mt8167
} // namespace audio
