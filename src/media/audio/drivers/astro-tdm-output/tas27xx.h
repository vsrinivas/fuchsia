// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_ASTRO_TDM_OUTPUT_TAS27XX_H_
#define SRC_MEDIA_AUDIO_DRIVERS_ASTRO_TDM_OUTPUT_TAS27XX_H_

#include <lib/device-protocol/i2c-channel.h>

#include <memory>

#include <ddk/debug.h>

namespace audio {
namespace astro {

static constexpr uint8_t SW_RESET = 0x01;  // sw reset
static constexpr uint8_t PWR_CTL = 0x02;   // power control
static constexpr uint8_t PB_CFG2 = 0x05;   // pcm gain register
static constexpr uint8_t TDM_CFG0 = 0x0a;
static constexpr uint8_t TDM_CFG1 = 0x0b;
static constexpr uint8_t TDM_CFG2 = 0x0c;
static constexpr uint8_t TDM_CFG3 = 0x0d;
static constexpr uint8_t TDM_CFG4 = 0x0e;
static constexpr uint8_t TDM_CFG5 = 0x0f;
static constexpr uint8_t TDM_CFG6 = 0x10;
static constexpr uint8_t TDM_CFG7 = 0x11;
static constexpr uint8_t TDM_CFG8 = 0x12;
static constexpr uint8_t TDM_CFG9 = 0x13;
static constexpr uint8_t TDM_CFG10 = 0x14;
static constexpr uint8_t CLOCK_CFG = 0x3c;  // Clock Config

class Tas27xx : public std::unique_ptr<Tas27xx> {
 public:
  static std::unique_ptr<Tas27xx> Create(ddk::I2cChannel&& i2c);
  bool ValidGain(float gain);
  zx_status_t SetGain(float gain);
  float GetGain() const { return current_gain_; }
  float GetMinGain() const { return kMinGain; }
  float GetMaxGain() const { return kMaxGain; }
  float GetGainStep() const { return kGainStep; }

  virtual zx_status_t Init();  // virtual for unit testing.
  zx_status_t Reset();
  zx_status_t Standby();
  zx_status_t ExitStandby();
  zx_status_t Mute(bool mute);

 protected:
  Tas27xx(ddk::I2cChannel&& i2c) : i2c_(i2c) {}  // protected for unit testing.
  virtual ~Tas27xx() = default;                  // protected for unit testing.

 private:
  friend class std::default_delete<Tas27xx>;
  static constexpr float kMaxGain = 0;
  static constexpr float kMinGain = -100.0;
  static constexpr float kGainStep = 0.5;

  zx_status_t WriteReg(uint8_t reg, uint8_t value);
  uint8_t ReadReg(uint8_t reg);

  zx_status_t SetStandby(bool stdby);

  ddk::I2cChannel i2c_;

  float current_gain_ = 0;
};
}  // namespace astro
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_ASTRO_TDM_OUTPUT_TAS27XX_H_
