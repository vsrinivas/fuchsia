// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS5720_TAS5720_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS5720_TAS5720_H_

#include <lib/device-protocol/i2c-channel.h>
#include <lib/simple-codec/simple-codec-server.h>

#include <memory>
#include <optional>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpio.h>

namespace audio {

class Tas5720 : public SimpleCodecServer {
 public:
  explicit Tas5720(zx_device_t* device, const ddk::I2cChannel& i2c)
      : SimpleCodecServer(device), i2c_(i2c) {}
  virtual ~Tas5720() = default;

  // Implementation for SimpleCodecServer.
  zx_status_t Shutdown() override;

 protected:
  // Implementation for SimpleCodecServer.
  zx::status<DriverIds> Initialize() override;
  zx_status_t Reset() override;
  Info GetInfo() override;
  zx_status_t Stop() override;
  zx_status_t Start() override;
  bool IsBridgeable() override;
  void SetBridgedMode(bool enable_bridged_mode) override;
  std::vector<DaiSupportedFormats> GetDaiFormats() override;
  zx_status_t SetDaiFormat(const DaiFormat& format) override;
  GainFormat GetGainFormat() override;
  GainState GetGainState() override;
  void SetGainState(GainState state) override;
  PlugState GetPlugState() override;

 private:
  static constexpr float kMaxGain = 24.f + 0.f;        // Max digital + analog.
  static constexpr float kMinGain = -(103.5f + 7.1f);  // Min digital + analog.
  static constexpr float kGainStep = .5f;

  bool ValidGain(float gain);
  virtual zx_status_t SetGain(float gain);  // virtual for unit testing.
  bool ValidGain(float gain) const;
  zx_status_t Reinitialize();
  zx_status_t SetMuted(bool mute);
  zx_status_t SetSlot(uint8_t slot);
  zx_status_t WriteReg(uint8_t reg, uint8_t value);
  zx_status_t ReadReg(uint8_t reg, uint8_t* value);
  zx_status_t SetRateAndFormat();

  ddk::I2cChannel i2c_;
  bool started_ = false;
  bool i2s_ = false;
  uint32_t rate_ = 48'000;
  GainState gain_state_ = {};
  uint8_t tdm_slot_ = 0;
  uint32_t instance_count_ = 0;
};
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS5720_TAS5720_H_
