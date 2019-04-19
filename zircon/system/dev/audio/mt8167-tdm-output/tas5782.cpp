// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas5782.h"

#include <algorithm>
#include <memory>
#include <string.h>

#include <ddk/protocol/i2c-lib.h>
#include <ddk/protocol/i2c.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

namespace audio {
namespace mt8167 {

constexpr float Tas5782::kMaxGain;
constexpr float Tas5782::kMinGain;

std::unique_ptr<Tas5782> Tas5782::Create(ddk::I2cChannel i2c, uint32_t i2c_index) {
    fbl::AllocChecker ac;
    auto ptr = std::unique_ptr<Tas5782>(new (&ac) Tas5782(i2c));
    if (!ac.check()) {
        return nullptr;
    }
    return ptr;
}

zx_status_t Tas5782::Reset() {
    return WriteReg(0x01, 0x01);
}

zx_status_t Tas5782::SetGain(float gain) {
    gain = std::clamp(gain, kMinGain, kMaxGain);

    uint8_t gain_reg = static_cast<uint8_t>(48 - gain * 2);

    zx_status_t status;
    status = WriteReg(61, gain_reg); // Left gain.
    if (status != ZX_OK) {
        return status;
    }
    status = WriteReg(62, gain_reg); // Right gain.
    if (status != ZX_OK) {
        return status;
    }
    current_gain_ = gain;
    return status;
}

bool Tas5782::ValidGain(float gain) const {
    return (gain <= kMaxGain) && (gain >= kMinGain);
}

zx_status_t Tas5782::Init() {
    zx_status_t status;
    status = Standby();
    status = WriteReg(13, 0x10); // The PLL reference clock is SCLK.
    if (status != ZX_OK) {
        return status;
    }
    status = WriteReg(4, 0x01); // PLL for MCLK setting.
    if (status != ZX_OK) {
        return status;
    }
    status = WriteReg(40, 0x03); // I2S, 32 bits.
    if (status != ZX_OK) {
        return status;
    }
    status = WriteReg(42, 0x22); // Left DAC to Left ch, Right DAC to right channel.
    if (status != ZX_OK) {
        return status;
    }
    return ExitStandby();
}

zx_status_t Tas5782::Standby() {
    return WriteReg(0x02, 0x10);
}

zx_status_t Tas5782::ExitStandby() {
    return WriteReg(0x02, 0x00);
}

zx_status_t Tas5782::WriteReg(uint8_t reg, uint8_t value) {
    uint8_t write_buf[2];
    write_buf[0] = reg;
    write_buf[1] = value;
    return i2c_.WriteSync(write_buf, 2);
}
} // namespace mt8167
} // namespace audio
