// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DEVICE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DEVICE_H_

#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/mixer/no_op.h"
#include "src/media/audio/audio_core/testing/stub_device_registry.h"

namespace media::audio::testing {

class FakeAudioDevice : public AudioDevice {
 public:
  FakeAudioDevice(AudioDevice::Type type, ThreadingModel* threading_model, DeviceRegistry* registry)
      : AudioDevice(type, threading_model, registry) {}

  bool driver_info_fetched() { return driver_info_fetched_; }
  bool driver_config_complete() { return driver_config_complete_; }
  bool driver_start_complete() { return driver_start_complete_; }
  bool driver_stop_complete() { return driver_stop_complete_; }
  std::pair<bool, zx::time> driver_plug_state() { return {driver_plug_state_, driver_plug_time_}; }

  // Expose these for the tests.
  using AudioDevice::ForEachDestLink;
  using AudioDevice::ForEachSourceLink;

  // |media::audio::AudioDevice|
  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info, uint32_t set_flags) override {}
  void OnWakeup() override {}
  void OnDriverInfoFetched() override { driver_info_fetched_ = true; }
  void OnDriverConfigComplete() override { driver_config_complete_ = true; }
  void OnDriverStartComplete() override { driver_start_complete_ = true; }
  void OnDriverStopComplete() override { driver_stop_complete_ = true; }
  void OnDriverPlugStateChange(bool plugged, zx::time plug_time) override {
    driver_plug_state_ = plugged;
    driver_plug_time_ = plug_time;
  }

 private:
  bool driver_info_fetched_ = false;
  bool driver_config_complete_ = false;
  bool driver_start_complete_ = false;
  bool driver_stop_complete_ = false;
  bool driver_plug_state_ = false;
  zx::time driver_plug_time_;
};

class FakeAudioInput : public FakeAudioDevice {
 public:
  static fbl::RefPtr<FakeAudioInput> Create(ThreadingModel* threading_model,
                                            DeviceRegistry* registry) {
    return fbl::AdoptRef(new FakeAudioInput(threading_model, registry));
  }

  FakeAudioInput(ThreadingModel* threading_model, DeviceRegistry* registry)
      : FakeAudioDevice(Type::Input, threading_model, registry) {}
};

class FakeAudioOutput : public FakeAudioDevice {
 public:
  static fbl::RefPtr<FakeAudioOutput> Create(ThreadingModel* threading_model,
                                             DeviceRegistry* registry) {
    return fbl::AdoptRef(new FakeAudioOutput(threading_model, registry));
  }

  FakeAudioOutput(ThreadingModel* threading_model, DeviceRegistry* registry)
      : FakeAudioDevice(Type::Output, threading_model, registry) {}

  // Required, to allocate and set the mixer+bookkeeping
  zx_status_t InitializeSourceLink(const fbl::RefPtr<AudioLink>& link) final {
    link->set_mixer(std::make_unique<audio::mixer::NoOp>());
    return ZX_OK;
  }

  void SetMinLeadTime(zx::duration min_lead_time) { min_lead_time_ = min_lead_time; }

  // Must implement, because this class descends from AudioDevice, not AudioOutput
  zx::duration min_lead_time() const override { return min_lead_time_; }

 private:
  zx::duration min_lead_time_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DEVICE_H_
