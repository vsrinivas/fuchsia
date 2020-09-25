// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS58XX_TAS58XX_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS58XX_TAS58XX_H_

#include <lib/device-protocol/i2c-channel.h>
#include <lib/simple-codec/simple-codec-server.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <ti/ti-audio.h>

#include "ddktl/suspend-txn.h"

namespace audio {

class Tas58xx : public SimpleCodecServer {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit Tas58xx(zx_device_t* device, const ddk::I2cChannel& i2c);

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

  std::atomic<bool> initialized_ = false;  // Protected for unit tests.

 private:
  static constexpr float kMaxGain = 24.0;
  static constexpr float kMinGain = -103.0;
  static constexpr float kGainStep = 0.5;

  zx_status_t WriteReg(uint8_t reg, uint8_t value) TA_REQ(lock_);
  zx_status_t ReadReg(uint8_t reg, uint8_t* value) TA_REQ(lock_);
  zx_status_t UpdateReg(uint8_t reg, uint8_t mask, uint8_t value) TA_REQ(lock_);

  ddk::I2cChannel i2c_;
  GainState gain_state_ = {};
  fbl::Mutex lock_;
  metadata::ti::TasConfig metadata_ = {};
};
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS58XX_TAS58XX_H_
