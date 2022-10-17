// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_MAX98373_MAX98373_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_MAX98373_MAX98373_H_

#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/simple-codec/simple-codec-server.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>

#include <memory>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "ddktl/suspend-txn.h"

namespace audio {

class Max98373 : public SimpleCodecServer {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit Max98373(zx_device_t* device, ddk::I2cChannel i2c, ddk::GpioProtocolClient codec_reset)
      : SimpleCodecServer(device), i2c_(std::move(i2c)), codec_reset_(std::move(codec_reset)) {}
  // Implementation for SimpleCodecServer.
  zx_status_t Shutdown() override;

 protected:
  // Implementation for SimpleCodecServer.
  zx::result<DriverIds> Initialize() override;
  zx_status_t Reset() override;
  Info GetInfo() override;
  zx_status_t Stop() override { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t Start() override { return ZX_ERR_NOT_SUPPORTED; }
  DaiSupportedFormats GetDaiFormats() override;
  zx::result<CodecFormatInfo> SetDaiFormat(const DaiFormat& format) override;
  GainFormat GetGainFormat() override;
  GainState GetGainState() override;
  void SetGainState(GainState state) override;

  zx_status_t HardwareReset();  // Protected for unit tests.

  std::atomic<bool> initialized_ = false;  // Protected for unit tests.

 private:
  static constexpr float kMaxGain = 0.0;
  static constexpr float kMinGain = -63.5;
  static constexpr float kGainStep = 0.5;

  zx_status_t WriteReg(uint16_t reg, uint8_t value);
  zx_status_t ReadReg(uint16_t reg, uint8_t* value);
  uint8_t getTdmClockRatio(uint32_t number_of_channels, uint8_t bits_per_slot);

  GainState gain_state_ = {};
  ddk::I2cChannel i2c_;
  ddk::GpioProtocolClient codec_reset_;
};
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_MAX98373_MAX98373_H_
