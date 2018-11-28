// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/debug.h>
#include <ddk/protocol/i2c.h>

#include <ddktl/pdev.h>

#include <fbl/unique_ptr.h>

namespace audio {

class Tas5760 {
public:
    static fbl::unique_ptr<Tas5760> Create(const pdev_protocol_t& pdev, uint32_t index);

    explicit Tas5760(i2c_protocol_t i2c)
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
    static constexpr float kMinGain = -103.5;
    static constexpr float kGainStep = 0.5;

    zx_status_t WriteReg(uint8_t reg, uint8_t value);
    zx_status_t ReadReg(uint8_t reg, uint8_t* value);
    zx_status_t SetStandby(bool stdby);

    i2c_protocol_t i2c_;
    float current_gain_ = 0;
};
} // namespace audio
