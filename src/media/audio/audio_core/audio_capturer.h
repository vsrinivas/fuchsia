// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CAPTURER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CAPTURER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/media/audio/audio_core/base_capturer.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"

namespace media::audio {

class AudioCapturer : public BaseCapturer,
                      public fuchsia::media::audio::GainControl,
                      public StreamVolume {
 public:
  AudioCapturer(fuchsia::media::AudioCapturerConfiguration configuration,
                std::optional<Format> format,
                fidl::InterfaceRequest<fuchsia::media::AudioCapturer> request, Context* context);

  ~AudioCapturer() override;

 private:
  // |media::audio::BaseCapturer|
  void ReportStart() override;
  void ReportStop() override;
  void OnStateChanged(State old_state, State new_state) override;
  void SetRoutingProfile(bool routable) override;

  // |fuchsia::media::AudioCapturer|
  void SetReferenceClock(zx::clock ref_clock) final;
  void SetPcmStreamType(fuchsia::media::AudioStreamType stream_type) final;
  void BindGainControl(fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) final;
  void SetUsage(fuchsia::media::AudioCaptureUsage usage) final;

  // |fuchsia::media::audio::GainControl|
  void SetGain(float gain_db) final;
  void SetGainWithRamp(float gain_db, int64_t duration_ns,
                       fuchsia::media::audio::RampType ramp_type) final {
    FX_NOTIMPLEMENTED();
  }
  void SetMute(bool mute) final;
  void NotifyGainMuteChanged();

  // |media::audio::AudioObject|
  void OnLinkAdded() override;
  std::optional<StreamUsage> usage() const override {
    return {StreamUsage::WithCaptureUsage(usage_)};
  }

  // |media::audio::StreamVolume|
  bool GetStreamMute() const final;
  fuchsia::media::Usage GetStreamUsage() const final;
  void RealizeVolume(VolumeCommand volume_command) final;

  CaptureUsage capture_usage() const {
    return loopback_ ? CaptureUsage::LOOPBACK : CaptureUsageFromFidlCaptureUsage(usage_);
  }

  fidl::BindingSet<fuchsia::media::audio::GainControl> gain_control_bindings_;

  const bool loopback_;
  bool mute_;
  std::atomic<float> stream_gain_db_;
  fuchsia::media::AudioCaptureUsage usage_ = fuchsia::media::AudioCaptureUsage::FOREGROUND;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CAPTURER_H_
