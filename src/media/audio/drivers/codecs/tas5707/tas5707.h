// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS5707_TAS5707_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS5707_TAS5707_H_

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/simple-codec/simple-codec-server.h>

#include <memory>
#include <optional>

#include <ddktl/device.h>
#include <ti/ti-audio.h>

namespace audio {

class Tas5707 : public SimpleCodecServer {
 public:
  explicit Tas5707(zx_device_t* device, ddk::I2cChannel i2c)
      : SimpleCodecServer(device), i2c_(std::move(i2c)) {}
  virtual ~Tas5707() = default;

  // Implementation for SimpleCodecServer.
  zx_status_t Shutdown() override;

 protected:
  // Implementation for SimpleCodecServer.
  zx::result<DriverIds> Initialize() override;
  zx_status_t Reset() override;
  Info GetInfo() override;
  zx_status_t Stop() override;
  zx_status_t Start() override;
  DaiSupportedFormats GetDaiFormats() override;
  zx::result<CodecFormatInfo> SetDaiFormat(const DaiFormat& format) override;
  GainFormat GetGainFormat() override;
  GainState GetGainState() override;
  void SetGainState(GainState state) override;

 private:
  static constexpr float kMaxGain = 24.f;
  static constexpr float kMinGain = -79.f;
  static constexpr float kGainStep = .5f;

  zx_status_t WriteReg(uint8_t reg, uint8_t value);
  zx_status_t ReadReg(uint8_t reg, uint8_t* value);

  ddk::I2cChannel i2c_;
  GainState gain_state_ = {};
  metadata::ti::TasConfig metadata_ = {};
  uint32_t instance_count_ = 0;
};
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS5707_TAS5707_H_
