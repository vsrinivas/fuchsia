// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas5805.h"

#include <algorithm>
#include <memory>
#include <string.h>

#include <ddk/protocol/i2c-lib.h>
#include <ddk/protocol/i2c.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

namespace {
// clang-format off
constexpr uint8_t kRegSelectPage  = 0x00;
constexpr uint8_t kRegReset       = 0x01;
constexpr uint8_t kRegDeviceCtrl1 = 0x02;
constexpr uint8_t kRegDeviceCtrl2 = 0x03;
constexpr uint8_t kRegSapCtrl1    = 0x33;
constexpr uint8_t kRegDigitalVol  = 0x4C;

constexpr uint8_t kRegResetBitCtrl             = 0x01;
constexpr uint8_t kRegDeviceCtrl1BitsPbtlMode  = 0x04;
constexpr uint8_t kRegDeviceCtrl1Bits1SpwMode  = 0x01;
constexpr uint8_t kRegSapCtrl1Bits32bits       = 0x03;
constexpr uint8_t kRegDeviceCtrl2BitsDeepSleep = 0x00;
constexpr uint8_t kRegDeviceCtrl2BitsPlay      = 0x03;
// clang-format on
} // namespace

namespace audio {
namespace mt8167 {

std::unique_ptr<Tas5805> Tas5805::Create(ddk::I2cChannel i2c, uint32_t i2c_index) {
    fbl::AllocChecker ac;
    auto ptr = std::unique_ptr<Tas5805>(new (&ac) Tas5805(i2c));
    if (!ac.check()) {
        return nullptr;
    }
    return ptr;
}

zx_status_t Tas5805::Reset() {
    return WriteReg(kRegReset, kRegResetBitCtrl);
}

zx_status_t Tas5805::SetGain(float gain) {
    gain = std::clamp(gain, kMinGain, kMaxGain);

    uint8_t gain_reg = static_cast<uint8_t>(48 - gain * 2);

    zx_status_t status = WriteReg(kRegDigitalVol, gain_reg);
    if (status != ZX_OK) {
        return status;
    }
    current_gain_ = gain;
    return status;
}

bool Tas5805::ValidGain(float gain) const {
    return (gain <= kMaxGain) && (gain >= kMinGain);
}

zx_status_t Tas5805::Init() {
    constexpr uint8_t defaults[][2] = {
        {kRegSelectPage, 0x00},
        {kRegDeviceCtrl1, kRegDeviceCtrl1BitsPbtlMode | kRegDeviceCtrl1Bits1SpwMode},
        {kRegSapCtrl1, kRegSapCtrl1Bits32bits},
    };
    for (auto& i : defaults) {
        zx_status_t status = WriteReg(i[0], i[1]);
        if (status != ZX_OK) {
            return status;
        }
    }
    return ExitStandby();
}

zx_status_t Tas5805::Standby() {
    zx_status_t status = WriteReg(kRegSelectPage, 0x00);
    if (status != ZX_OK) {
        return status;
    }
    return WriteReg(kRegDeviceCtrl2, kRegDeviceCtrl2BitsDeepSleep);
}

zx_status_t Tas5805::ExitStandby() {
    zx_status_t status = WriteReg(kRegSelectPage, 0x00);
    if (status != ZX_OK) {
        return status;
    }
    return WriteReg(kRegDeviceCtrl2, kRegDeviceCtrl2BitsPlay);
}

zx_status_t Tas5805::WriteReg(uint8_t reg, uint8_t value) {
    uint8_t write_buf[2];
    write_buf[0] = reg;
    write_buf[1] = value;
    return i2c_.WriteSync(write_buf, 2);
}
} // namespace mt8167
} // namespace audio
