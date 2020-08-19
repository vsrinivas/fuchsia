// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DEVICE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DEVICE_H_

#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/device_registry.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/mixer/no_op.h"

namespace media::audio::testing {

class FakeAudioDevice : public AudioDevice {
 public:
  FakeAudioDevice(AudioDevice::Type type, ThreadingModel* threading_model, DeviceRegistry* registry,
                  LinkMatrix* link_matrix)
      : AudioDevice(type, threading_model, registry, link_matrix,
                    std::make_unique<AudioDriverV1>(this)),
        mix_domain_(threading_model->AcquireMixDomain()) {}

  bool driver_info_fetched() { return driver_info_fetched_; }
  bool driver_config_complete() { return driver_config_complete_; }
  bool driver_start_complete() { return driver_start_complete_; }
  bool driver_stop_complete() { return driver_stop_complete_; }
  std::pair<bool, zx::time> driver_plug_state() { return {driver_plug_state_, driver_plug_time_}; }

  // |media::audio::AudioDevice|
  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info,
                       fuchsia::media::AudioGainValidFlags set_flags) override {}
  void OnWakeup() override {}
  void OnDriverInfoFetched() override { driver_info_fetched_ = true; }
  void OnDriverConfigComplete() override { driver_config_complete_ = true; }
  void OnDriverStartComplete() override { driver_start_complete_ = true; }
  void OnDriverStopComplete() override { driver_stop_complete_ = true; }
  void OnDriverPlugStateChange(bool plugged, zx::time plug_time) override {
    driver_plug_state_ = plugged;
    driver_plug_time_ = plug_time;
  }

 protected:
  ThreadingModel::OwnedDomainPtr mix_domain_;

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
  static std::shared_ptr<FakeAudioInput> Create(ThreadingModel* threading_model,
                                                DeviceRegistry* registry, LinkMatrix* link_matrix) {
    return std::make_shared<FakeAudioInput>(threading_model, registry, link_matrix);
  }

  FakeAudioInput(ThreadingModel* threading_model, DeviceRegistry* registry, LinkMatrix* link_matrix)
      : FakeAudioDevice(Type::Input, threading_model, registry, link_matrix) {}
};

class FakeAudioOutput : public FakeAudioDevice {
 public:
  static std::shared_ptr<FakeAudioOutput> Create(ThreadingModel* threading_model,
                                                 DeviceRegistry* registry,
                                                 LinkMatrix* link_matrix) {
    return std::make_shared<FakeAudioOutput>(threading_model, registry, link_matrix);
  }

  FakeAudioOutput(ThreadingModel* threading_model, DeviceRegistry* registry,
                  LinkMatrix* link_matrix)
      : FakeAudioDevice(Type::Output, threading_model, registry, link_matrix) {}

  fit::result<std::pair<std::shared_ptr<Mixer>, ExecutionDomain*>, zx_status_t>
  InitializeSourceLink(const AudioObject& source, std::shared_ptr<ReadableStream> stream) override {
    stream->SetMinLeadTime(min_lead_time_);
    stream_ = std::move(stream);
    return fit::ok(std::make_pair(mixer_, mix_domain_.get()));
  }
  void SetMinLeadTime(zx::duration min_lead_time) { min_lead_time_ = min_lead_time; }

  const std::shared_ptr<ReadableStream>& stream() const { return stream_; }

  // Must implement, because this class descends from AudioDevice, not AudioOutput
  zx::duration min_lead_time() const override { return min_lead_time_; }

 private:
  std::shared_ptr<ReadableStream> stream_;
  std::shared_ptr<mixer::NoOp> mixer_ = std::make_shared<mixer::NoOp>();
  zx::duration min_lead_time_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DEVICE_H_
