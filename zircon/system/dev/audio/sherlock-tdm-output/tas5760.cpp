// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <ddk/protocol/i2c-lib.h>

#include <fbl/algorithm.h>

#include "tas5760.h"

namespace audio {

constexpr float Tas5760::kMaxGain;
constexpr float Tas5760::kMinGain;
// clang-format off
constexpr uint8_t kRegPowerControl        = 0x01;
constexpr uint8_t kRegDigitalControl      = 0x02;
constexpr uint8_t kRegVolumeControlCnf    = 0x03;
constexpr uint8_t kRegLeftControl         = 0x04;
constexpr uint8_t kRegRightControl        = 0x05;
constexpr uint8_t kRegAnalogControl       = 0x06;
constexpr uint8_t kRegFaultCfgErrorStatus = 0x08;
constexpr uint8_t kRegDigitalClipper2     = 0x10;
constexpr uint8_t kRegDigitalClipper1     = 0x11;
// clang-format on

// static
fbl::unique_ptr<Tas5760> Tas5760::Create(ddk::I2cChannel i2c) {
    fbl::AllocChecker ac;
    auto ptr = fbl::make_unique_checked<Tas5760>(&ac, i2c);
    if (!ac.check()) {
        return nullptr;
    }
    return ptr;
}

zx_status_t Tas5760::Reset() {
    return ZX_OK;
}

zx_status_t Tas5760::SetGain(float gain) {
    gain = fbl::clamp(gain, kMinGain, kMaxGain);
    // Datasheet: "DVC [Hex Value] = 0xCF + (DVC [dB] / 0.5 [dB] )".
    uint8_t gain_reg = static_cast<uint8_t>(0xCF + gain / .5f);
    zx_status_t status = WriteReg(kRegLeftControl, gain_reg);
    if (status != ZX_OK) {
        return status;
    }
    status = WriteReg(kRegRightControl, gain_reg);
    if (status != ZX_OK) {
        return status;
    }
    current_gain_ = gain;
    return status;
}

bool Tas5760::ValidGain(float gain) const {
    return (gain <= kMaxGain) && (gain >= kMinGain);
}

zx_status_t Tas5760::Init(std::optional<uint8_t> slot) {
    if (slot.has_value()) {
        return ZX_ERR_NOT_SUPPORTED; // Always use L+R (slots 0 and 1).
    }
    Standby();
    WriteReg(kRegDigitalControl, 0x05);   // no HPF, no boost, Single Speed, Stereo Left Justified.
    WriteReg(kRegVolumeControlCnf, 0x80); // Fade enabled.
    WriteReg(kRegAnalogControl, 0x51);    // PWM rate 16 x lrclk.
    WriteReg(kRegDigitalClipper2, 0xFF);  // Disabled.
    WriteReg(kRegDigitalClipper1, 0xFC);  // Disabled.
    ExitStandby();
    uint8_t val = 0;
    ReadReg(kRegFaultCfgErrorStatus, &val);
    if (val != 0x00) {
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

zx_status_t Tas5760::Standby() {
    uint8_t r;
    ReadReg(kRegPowerControl, &r);
    r = static_cast<uint8_t>(r & ~(0x01)); // SPK_SD.
    r = static_cast<uint8_t>(r | (0x02));  // SPK_SLEEP.
    WriteReg(kRegPowerControl, r);
    return ZX_OK;
}

zx_status_t Tas5760::ExitStandby() {
    uint8_t r;
    ReadReg(kRegPowerControl, &r);
    r = static_cast<uint8_t>(r | 0x01); // SPK_SD.
    WriteReg(kRegPowerControl, r);
    r = static_cast<uint8_t>(r & ~(0x02)); // SPK_SLEEP.
    WriteReg(kRegPowerControl, r);
    return ZX_OK;
}

zx_status_t Tas5760::WriteReg(uint8_t reg, uint8_t value) {
    uint8_t write_buf[2];
    write_buf[0] = reg;
    write_buf[1] = value;
    return i2c_.WriteSync(write_buf, 2);
}

zx_status_t Tas5760::ReadReg(uint8_t reg, uint8_t* value) {
    return i2c_.WriteReadSync(&reg, 1, value, 1);
}
} // audio
