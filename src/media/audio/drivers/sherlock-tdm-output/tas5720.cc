// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas5720.h"

#include <lib/device-protocol/i2c.h>
#include <string.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

namespace audio {

constexpr float Tas5720::kMaxGain;
constexpr float Tas5720::kMinGain;
// clang-format off
constexpr uint8_t kRegPowerControl        = 0x01;
constexpr uint8_t kRegDigitalControl1     = 0x02;
constexpr uint8_t kRegDigitalControl2     = 0x03;
constexpr uint8_t kRegVolumeControl       = 0x04;
constexpr uint8_t kRegAnalogControl       = 0x06;
constexpr uint8_t kRegFaultCfgErrorStatus = 0x08;
constexpr uint8_t kRegDigitalClipper2     = 0x10;
constexpr uint8_t kRegDigitalClipper1     = 0x11;
// clang-format on

// static
std::unique_ptr<Tas5720> Tas5720::Create(ddk::I2cChannel i2c) {
  fbl::AllocChecker ac;
  auto ptr = fbl::make_unique_checked<Tas5720>(&ac, i2c);
  if (!ac.check()) {
    return nullptr;
  }
  return ptr;
}

zx_status_t Tas5720::Reset() { return ZX_OK; }

zx_status_t Tas5720::Mute(bool mute) {
  uint8_t val = 0;
  auto status = ReadReg(kRegDigitalControl2, &val);
  if (status != ZX_OK) {
    return status;
  }
  return WriteReg(kRegDigitalControl2, static_cast<uint8_t>(mute ? (val | 0x10) : (val & ~0x10)));
}

zx_status_t Tas5720::SetGain(float gain) {
  gain = fbl::clamp(gain, kMinGain, kMaxGain);
  // Datasheet: "DVC [Hex Value] = 0xCF + (DVC [dB] / 0.5 [dB] )".
  uint8_t gain_reg = static_cast<uint8_t>(0xCF + gain / .5f);
  zx_status_t status;
  status = WriteReg(kRegVolumeControl, gain_reg);
  if (status != ZX_OK) {
    return status;
  }
  current_gain_ = gain;
  return status;
}

bool Tas5720::ValidGain(float gain) const { return (gain <= kMaxGain) && (gain >= kMinGain); }

zx_status_t Tas5720::Init(std::optional<uint8_t> slot) {
  Standby();
  WriteReg(kRegDigitalControl1, 0x45);  // Use Slot, Stereo Left Justified.
  if (!slot.has_value() || slot.value() >= 8) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  WriteReg(kRegDigitalControl2, slot.value() | 0x10);  // Muted.
  WriteReg(kRegAnalogControl, 0x55);                   // PWM rate 16 x lrclk, gain 20.7 dBV.
  WriteReg(kRegDigitalClipper2, 0xFF);                 // Disabled.
  WriteReg(kRegDigitalClipper1, 0xFC);                 // Disabled.
  ExitStandby();
  SetGain(-12.f);                                      // Conservative default gain.
  uint8_t val = 0;
  ReadReg(kRegFaultCfgErrorStatus, &val);
  if (val != 0x00) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t Tas5720::Standby() {
  uint8_t r;
  ReadReg(kRegPowerControl, &r);
  r = static_cast<uint8_t>(r & ~(0x01));  // SPK_SD.
  r = static_cast<uint8_t>(r | (0x02));   // SPK_SLEEP.
  WriteReg(kRegPowerControl, r);
  return ZX_OK;
}

zx_status_t Tas5720::ExitStandby() {
  uint8_t r;
  ReadReg(kRegPowerControl, &r);
  r = static_cast<uint8_t>(r | 0x01);  // SPK_SD.
  WriteReg(kRegPowerControl, r);
  r = static_cast<uint8_t>(r & ~(0x02));  // SPK_SLEEP.
  WriteReg(kRegPowerControl, r);
  return ZX_OK;
}

zx_status_t Tas5720::WriteReg(uint8_t reg, uint8_t value) {
  uint8_t write_buf[2];
  write_buf[0] = reg;
  write_buf[1] = value;
  return i2c_.WriteSync(write_buf, 2);
}

zx_status_t Tas5720::ReadReg(uint8_t reg, uint8_t* value) {
  return i2c_.WriteReadSync(&reg, 1, value, 1);
}
}  // namespace audio
