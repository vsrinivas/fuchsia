// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_MT8167_TDM_INPUT_TLV320ADC_H_
#define SRC_MEDIA_AUDIO_DRIVERS_MT8167_TDM_INPUT_TLV320ADC_H_

#include <lib/device-protocol/i2c-channel.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/protocol/i2c.h>

namespace audio {
namespace mt8167 {

class Tlv320adc {
 public:
  static std::unique_ptr<Tlv320adc> Create(const ddk::I2cChannel& i2c, uint32_t i2c_index);

  explicit Tlv320adc(const ddk::I2cChannel& i2c) : i2c_(i2c) {}

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
  static constexpr float kMaxGain = 20.0;
  static constexpr float kMinGain = -12.0;
  static constexpr float kGainStep = 0.5;

  zx_status_t WriteReg(uint8_t page, uint8_t reg, uint8_t value);

  ddk::I2cChannel i2c_;

  float current_gain_ = 0;
};
}  // namespace mt8167
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_MT8167_TDM_INPUT_TLV320ADC_H_
