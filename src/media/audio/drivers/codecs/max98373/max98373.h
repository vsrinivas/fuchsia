// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_MAX98373_MAX98373_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_MAX98373_MAX98373_H_

#include <lib/device-protocol/i2c-channel.h>
#include <lib/simple-codec/simple-codec-server.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/codec.h>
#include <ddktl/protocol/gpio.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "ddktl/suspend-txn.h"

namespace audio {

class Max98373 : public SimpleCodecServer {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit Max98373(zx_device_t* device, const ddk::I2cChannel& i2c,
                    const ddk::GpioProtocolClient& codec_reset)
      : SimpleCodecServer(device), i2c_(i2c), codec_reset_(codec_reset) {}
  // Implementation for SimpleCodecServer.
  zx_status_t Shutdown() override;

 protected:
  // Implementation for SimpleCodecServer.
  zx::status<DriverIds> Initialize() override;
  zx_status_t Reset() override;
  Info GetInfo() override;
  zx_status_t Stop() override { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t Start() override { return ZX_ERR_NOT_SUPPORTED; }
  bool IsBridgeable() override;
  void SetBridgedMode(bool enable_bridged_mode) override;
  std::vector<DaiSupportedFormats> GetDaiFormats() override;
  zx_status_t SetDaiFormat(const DaiFormat& format) override;
  GainFormat GetGainFormat() override;
  GainState GetGainState() override;
  void SetGainState(GainState state) override;
  PlugState GetPlugState() override;

  zx_status_t HardwareReset();  // Protected for unit tests.

  std::atomic<bool> initialized_ = false;  // Protected for unit tests.

 private:
  static constexpr float kMaxGain = 0.0;
  static constexpr float kMinGain = -63.5;
  static constexpr float kGainStep = 0.5;

  zx_status_t WriteReg(uint16_t reg, uint8_t value) TA_REQ(lock_);
  zx_status_t ReadReg(uint16_t reg, uint8_t* value) TA_REQ(lock_);
  int Thread();

  GainState gain_state_ = {};
  ddk::I2cChannel i2c_;
  ddk::GpioProtocolClient codec_reset_;
  thrd_t thread_;
  fbl::Mutex lock_;
};
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_MAX98373_MAX98373_H_
