// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas5720.h"

#include <lib/device-protocol/i2c.h>
#include <string.h>

#include <algorithm>
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
  gain = std::clamp(gain, kMinGain, kMaxGain);
  float digital_gain = gain;
  // For gains lower than 0dB we lower the analog gain first.
  uint8_t analog_setting = 3;  // 26.3dBV.
  if (gain >= 0.f) {
    // Apply the gain directly as digital volume, keep analog_setting at 3.
  } else if (gain >= -2.8f) {
    digital_gain += 2.8f;
    analog_setting = 2;  // 23.5dBV.
  } else if (gain >= -5.6f) {
    digital_gain += 5.6f;
    analog_setting = 1;  // 20.7dBV.
  } else {
    digital_gain += 7.1f;
    analog_setting = 0;  // 19.2dBV.
  }
  constexpr uint8_t kPwmRate = 0x05;  // 16 x lrclk.
  constexpr uint8_t kReserved = 0x01;
  auto status = WriteReg(kRegAnalogControl,
                         static_cast<uint8_t>((kPwmRate << 4) | (analog_setting << 2) | kReserved));
  if (status != ZX_OK) {
    return status;
  }
  // Datasheet: "DVC [Hex Value] = 0xCF + (DVC [dB] / 0.5 [dB] )".
  uint8_t gain_reg = static_cast<uint8_t>(0xCF + digital_gain / .5f);
  status = WriteReg(kRegVolumeControl, gain_reg);
  if (status != ZX_OK) {
    return status;
  }
  current_gain_ = gain;
  return status;
}

bool Tas5720::ValidGain(float gain) const { return (gain <= kMaxGain) && (gain >= kMinGain); }

zx_status_t Tas5720::Init(std::optional<uint8_t> slot, uint32_t rate) {
  if (rate != 48000 && rate != 96000) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto status = Standby();
  if (status != ZX_OK) {
    return status;
  }
  // Use Slot, Stereo Left Justified. If 96kHz set double rate.
  status = WriteReg(kRegDigitalControl1, ((rate == 96000) ? 0x08 : 0) | 0x45);
  if (status != ZX_OK) {
    return status;
  }
  if (!slot.has_value() || slot.value() >= 8) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  status = WriteReg(kRegDigitalControl2, slot.value() | 0x10);  // Muted.
  if (status != ZX_OK) {
    return status;
  }
  constexpr uint8_t kAnalogSetting = 3;  // 26.3dBV.
  constexpr uint8_t kPwmRate = 0x05;     // 16 x lrclk.
  constexpr uint8_t kReserved = 0x01;
  status = WriteReg(kRegAnalogControl,
                    static_cast<uint8_t>((kPwmRate << 4) | (kAnalogSetting << 2) | kReserved));
  if (status != ZX_OK) {
    return status;
  }
  status = WriteReg(kRegDigitalClipper2, 0xFF);  // Disabled.
  if (status != ZX_OK) {
    return status;
  }
  status = WriteReg(kRegDigitalClipper1, 0xFC);  // Disabled.
  if (status != ZX_OK) {
    return status;
  }
  status = ExitStandby();
  if (status != ZX_OK) {
    return status;
  }
  return SetGain(-7.1f);  // Conservative default gain.
}

zx_status_t Tas5720::Standby() {
  uint8_t r;
  auto status = ReadReg(kRegPowerControl, &r);
  if (status != ZX_OK) {
    return status;
  }
  r = static_cast<uint8_t>(r & ~(0x01));  // SPK_SD.
  r = static_cast<uint8_t>(r | (0x02));   // SPK_SLEEP.
  return WriteReg(kRegPowerControl, r);
}

zx_status_t Tas5720::ExitStandby() {
  uint8_t r;
  auto status = ReadReg(kRegPowerControl, &r);
  if (status != ZX_OK) {
    return status;
  }
  r = static_cast<uint8_t>(r | 0x01);  // SPK_SD.
  status = WriteReg(kRegPowerControl, r);
  if (status != ZX_OK) {
    return status;
  }
  r = static_cast<uint8_t>(r & ~(0x02));  // SPK_SLEEP.
  return WriteReg(kRegPowerControl, r);
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
