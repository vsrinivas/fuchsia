// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS5782_TAS5782_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS5782_TAS5782_H_

#include <lib/device-protocol/i2c-channel.h>
#include <lib/simple-codec/simple-codec-server.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpio.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "ddktl/suspend-txn.h"

namespace audio {

class Tas5782 : public SimpleCodecServer {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit Tas5782(zx_device_t* device, const ddk::I2cChannel& i2c,
                   const ddk::GpioProtocolClient& codec_reset,
                   const ddk::GpioProtocolClient& codec_mute)
      : SimpleCodecServer(device), i2c_(i2c), codec_reset_(codec_reset), codec_mute_(codec_mute) {}

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

  std::atomic<bool> initialized_ = false;  // Protected for unit tests.

 private:
  static constexpr float kMaxGain = 24.0;
  static constexpr float kMinGain = -103.0;
  static constexpr float kGainStep = 0.5;

  zx_status_t WriteReg(uint8_t reg, uint8_t value) TA_REQ(lock_);

  ddk::I2cChannel i2c_;
  ddk::GpioProtocolClient codec_reset_;
  ddk::GpioProtocolClient codec_mute_;

  GainState gain_state_ = {};
  fbl::Mutex lock_;
};
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS5782_TAS5782_H_
