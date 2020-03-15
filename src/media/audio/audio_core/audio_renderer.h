// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_RENDERER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_RENDERER_H_

#include <lib/fidl/cpp/binding_set.h>

#include "src/media/audio/audio_core/base_renderer.h"
#include "src/media/audio/audio_core/stream_usage.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"

namespace media::audio {

class AudioRenderer : public BaseRenderer,
                      public fuchsia::media::audio::GainControl,
                      public StreamVolume {
 public:
  AudioRenderer(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request,
                Context* context);
  ~AudioRenderer();

 private:
  // |media::audio::AudioObject|
  const std::shared_ptr<Format>& format() const final { return format_; }
  std::optional<StreamUsage> usage() const override {
    return {StreamUsage::WithRenderUsage(usage_)};
  }
  void OnLinkAdded() override;

  // |fuchsia::media::AudioRenderer|
  void SetReferenceClock(zx::clock ref_clock) final;
  void SetPcmStreamType(fuchsia::media::AudioStreamType format) final;
  void SetUsage(fuchsia::media::AudioRenderUsage usage) override;
  void BindGainControl(fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) final;

  // |media::audio::BaseRenderer|
  void ReportStart() final;
  void ReportStop() final;
  void Shutdown() final;

  // |media::audio::StreamVolume|
  bool GetStreamMute() const final;
  fuchsia::media::Usage GetStreamUsage() const final;
  void RealizeVolume(VolumeCommand volume_command) final;

  // |fuchsia::media::audio::GainControl|
  void SetGain(float gain_db) final;
  void SetGainWithRamp(float gain_db, int64_t duration_ns,
                       fuchsia::media::audio::RampType ramp_type) final;
  void SetMute(bool muted) final;
  void NotifyGainMuteChanged();
  // TODO(mpuryear): Notify on SetGainWithRamp.
  // TODO(mpuryear): consider EnableGainChangeEvents(bool), like MinLeadTime.

  bool mute_ = false;
  std::shared_ptr<Format> format_;

  fuchsia::media::AudioRenderUsage usage_ = fuchsia::media::AudioRenderUsage::MEDIA;

  class GainControlBinding : public fuchsia::media::audio::GainControl {
   public:
    static std::unique_ptr<GainControlBinding> Create(AudioRenderer* owner) {
      return std::unique_ptr<GainControlBinding>(new GainControlBinding(owner));
    }

    // |fuchsia::media::audio::GainControl|
    void SetGain(float gain_db) final;
    void SetGainWithRamp(float gain_db, int64_t duration_ns,
                         fuchsia::media::audio::RampType ramp_type) final;
    void SetMute(bool muted) final;

   private:
    friend class std::default_delete<GainControlBinding>;

    GainControlBinding(AudioRenderer* owner) : owner_(owner) {}
    ~GainControlBinding() override {}

    AudioRenderer* owner_;
  };

  friend class GainControlBinding;

  fidl::BindingSet<fuchsia::media::audio::GainControl, std::unique_ptr<GainControlBinding>>
      gain_control_bindings_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_RENDERER_H_
