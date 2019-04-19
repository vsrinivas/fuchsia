// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <ddk/debug.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/i2c-channel.h>

#include "codec.h"

namespace audio {
namespace mt8167 {

class Tas5805 final : public Codec {
public:
    static std::unique_ptr<Tas5805> Create(ddk::I2cChannel i2c, uint32_t i2c_index);

    explicit Tas5805(const ddk::I2cChannel& i2c)
        : i2c_(i2c) {}

    // Implementation of Codec interface.
    bool ValidGain(float gain) const override;
    zx_status_t SetGain(float gain) override;
    zx_status_t Init() override;
    zx_status_t Reset() override;
    zx_status_t Standby() override;
    zx_status_t ExitStandby() override;
    float GetGain() const override { return current_gain_; }
    float GetMinGain() const override { return kMinGain; }
    float GetMaxGain() const override { return kMaxGain; }
    float GetGainStep() const override { return kGainStep; }

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
