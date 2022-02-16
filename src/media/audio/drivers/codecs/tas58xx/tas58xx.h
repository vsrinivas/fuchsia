// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS58XX_TAS58XX_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS58XX_TAS58XX_H_

#include <fuchsia/hardware/i2c/c/banjo.h>
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
#include <ti/ti-audio.h>

#include "ddktl/suspend-txn.h"

namespace audio {

class Tas58xx : public SimpleCodecServer, public fuchsia::hardware::audio::SignalProcessing {
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
  DaiSupportedFormats GetDaiFormats() override;
  zx::status<CodecFormatInfo> SetDaiFormat(const DaiFormat& format) override;
  GainFormat GetGainFormat() override;
  GainState GetGainState() override;
  void SetGainState(GainState state) override;
  void GetProcessingElements(
      fuchsia::hardware::audio::SignalProcessing::GetProcessingElementsCallback callback) override;
  void SetProcessingElementState(
      uint64_t processing_element_id, fuchsia::hardware::audio::ProcessingElementState control,
      fuchsia::hardware::audio::SignalProcessing::SetProcessingElementStateCallback callback)
      override;
  void WatchProcessingElementState(
      uint64_t processing_element_id,
      fuchsia::hardware::audio::SignalProcessing::WatchProcessingElementStateCallback callback)
      override;
  void GetTopologies(
      fuchsia::hardware::audio::SignalProcessing::GetTopologiesCallback callback) override;
  void SetTopology(
      uint64_t topology_id,
      fuchsia::hardware::audio::SignalProcessing::SetTopologyCallback callback) override;
  void SignalProcessingConnect(fidl::InterfaceRequest<fuchsia::hardware::audio::SignalProcessing>
                                   signal_processing) override;

  // Protected for unit tests.
  zx_status_t SetBand(bool enabled, size_t index, uint32_t frequency, float Q, float gain_db);
  uint64_t GetTopologyId() { return kTopologyId; }
  uint64_t GetAglPeId() { return kAglPeId; }
  uint64_t GetEqPeId() { return kEqPeId; }

 private:
  static constexpr float kMaxGain = 24.0;
  static constexpr float kMinGain = -103.0;
  static constexpr float kGainStep = 0.5;
  static constexpr uint64_t kAglPeId = 1;
  static constexpr uint64_t kEqPeId = 2;
  static constexpr uint64_t kTopologyId = 1;
  static constexpr size_t kEqualizerNumberOfBands = 5;
  static constexpr uint32_t kEqualizerMinFrequency = 100;
  static constexpr uint32_t kEqualizerMaxFrequency = 20'000;
  static constexpr float kEqualizerMinGainDb = -5.f;
  static constexpr float kEqualizerMaxGainDb = 5.f;
  static constexpr float kSupportedQ = 1.f;

  zx_status_t WriteReg(uint8_t reg, uint8_t value) TA_REQ(lock_);
  zx_status_t WriteRegs(uint8_t* regs, size_t count) TA_REQ(lock_);
  zx_status_t ReadReg(uint8_t reg, uint8_t* value) TA_REQ(lock_);
  zx_status_t UpdateReg(uint8_t reg, uint8_t mask, uint8_t value) TA_REQ(lock_);
  zx_status_t SetEqualizerProcessingElement(fuchsia::hardware::audio::ProcessingElementState state);
  void SetAutomaticGainControlProcessingElement(
      fuchsia::hardware::audio::ProcessingElementState state);
  void SendWatchReply(
      fuchsia::hardware::audio::SignalProcessing::WatchProcessingElementStateCallback callback);

  ddk::I2cChannel i2c_ TA_GUARDED(lock_);
  GainState gain_state_ TA_GUARDED(lock_) = {};
  fbl::Mutex lock_;
  metadata::ti::TasConfig metadata_ = {};
  bool started_ = false;
  uint32_t number_of_channels_ = 2;
  uint32_t rate_ = 48'000;

  // AGL.
  bool last_agl_ TA_GUARDED(lock_) = false;
  std::optional<bool> last_reported_agl_ TA_GUARDED(lock_);
  std::optional<fidl::Binding<fuchsia::hardware::audio::SignalProcessing>>
      signal_processing_binding_;
  std::optional<fuchsia::hardware::audio::SignalProcessing::WatchProcessingElementStateCallback>
      agl_callback_;

  // Equalizer.
  uint32_t frequencies_[kEqualizerNumberOfBands] = {100, 250, 1'000, 4'000, 16'000};
  float gains_[kEqualizerNumberOfBands] = {0.f, 0.f, 0.f, 0.f, 0.f};
  bool band_enabled_[kEqualizerNumberOfBands] = {false, false, false, false, false};
  bool equalizer_enabled_ = true;
  std::optional<fuchsia::hardware::audio::SignalProcessing::WatchProcessingElementStateCallback>
      equalizer_callback_;
  bool last_equalizer_update_reported_ = false;
};
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS58XX_TAS58XX_H_
