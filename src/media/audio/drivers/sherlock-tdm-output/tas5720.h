// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_SHERLOCK_TDM_OUTPUT_TAS5720_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_SHERLOCK_TDM_OUTPUT_TAS5720_H_

#include <lib/device-protocol/i2c-channel.h>

#include <memory>
#include <optional>

#include <ddk/debug.h>
#include <ddk/protocol/i2c.h>

namespace audio {

class Tas5720 final {
 public:
  static std::unique_ptr<Tas5720> Create(ddk::I2cChannel i2c);

  explicit Tas5720(const ddk::I2cChannel& i2c) : i2c_(i2c) {}

  bool ValidGain(float gain);
  zx_status_t SetGain(float gain);
  bool ValidGain(float gain) const;
  zx_status_t Init(std::optional<uint8_t> slot);
  zx_status_t Reset();
  zx_status_t Standby();
  zx_status_t ExitStandby();
  float GetGain() { return current_gain_; }
  float GetMinGain() { return kMinGain; }
  float GetMaxGain() { return kMaxGain; }
  float GetGainStep() { return kGainStep; }
  zx_status_t Mute(bool mute);

 private:
  static constexpr float kMaxGain = 24.0;
  static constexpr float kMinGain = -103.5;
  static constexpr float kGainStep = 0.5;

  zx_status_t WriteReg(uint8_t reg, uint8_t value);
  zx_status_t ReadReg(uint8_t reg, uint8_t* value);
  zx_status_t SetStandby(bool stdby);

  ddk::I2cChannel i2c_;
  float current_gain_ = 0;
};
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_SHERLOCK_TDM_OUTPUT_TAS5720_H_
