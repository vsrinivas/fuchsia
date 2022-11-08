// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_RENDERER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_RENDERER_H_

#include <lib/fidl/cpp/binding_set.h>

#include <mutex>
#include <optional>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/shared/stream_usage.h"
#include "src/media/audio/audio_core/v1/base_renderer.h"
#include "src/media/audio/audio_core/v1/stream_volume_manager.h"
#include "src/media/audio/lib/analysis/dropout.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media::audio {

class AudioRenderer : public BaseRenderer,
                      public fuchsia::media::audio::GainControl,
                      public StreamVolume {
 public:
  static std::shared_ptr<AudioRenderer> Create(
      fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request,
      Context* context) {
    return std::make_shared<AudioRenderer>(std::move(audio_renderer_request), context);
  }

  // Callers should use the |Create| method instead, this is only public to enable std::make_shared.
  AudioRenderer(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request,
                Context* context);
  ~AudioRenderer() override;

 private:
  // |media::audio::AudioObject|
  std::optional<Format> format() const final { return format_; }
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
  // Needed for ramped Play/Pause transitions
  void AddPayloadBufferInternal(uint32_t id, zx::vmo payload_buffer) final;
  void RemovePayloadBufferInternal(uint32_t id) final;
  void SendPacketInternal(fuchsia::media::StreamPacket packet, SendPacketCallback callback) final;
  void DiscardAllPacketsInternal(DiscardAllPacketsCallback callback) final;
  void EnableMinLeadTimeEventsInternal(bool enabled) final;
  void GetMinLeadTimeInternal(GetMinLeadTimeCallback callback) final;
  void PlayInternal(zx::time reference_time, zx::time media_time, PlayCallback callback) final;
  void PauseInternal(PauseCallback callback) final;

  // |media::audio::StreamVolume|
  fuchsia::media::Usage GetStreamUsage() const final;
  void RealizeVolume(VolumeCommand volume_command) final;

  // |fuchsia::media::audio::GainControl|
  void SetGain(float gain_db) final;
  void SetGainWithRamp(float gain_db, int64_t duration_ns,
                       fuchsia::media::audio::RampType ramp_type) final;
  void SetMute(bool muted) final;
  void NotifyGainMuteChanged();
  // TODO(mpuryear): Notify on SetGainWithRamp.

  void SetGainInternal(float gain_db);
  void SetGainWithRampInternal(float gain_db, int64_t duration_ns,
                               fuchsia::media::audio::RampType ramp_type);
  void SetMuteInternal(bool muted);

  void SerializeWithPause(fit::closure callback);

  // GainRamp/StreamGainCommand are used by PostStreamGainMute, while doing Play/Pause/SetGain.
  // Smoothly change gain from its current value to end_gain_db, over the specified duration.
  struct GainRamp {
    // The target gain for this ramp, in decibels.
    float end_gain_db;
    zx::duration duration;
    fuchsia::media::audio::RampType ramp_type = fuchsia::media::audio::RampType::SCALE_LINEAR;
  };
  // A command to realize gain changes.
  struct StreamGainCommand {
    // Gain to be set immediately, in decibels.
    std::optional<float> gain_db;
    // A ramp with which to apply a subsequent gain change, after setting the 'gain_db' above.
    std::optional<GainRamp> ramp;
    // Independent of gain_db or ramping, is this stream muted.
    std::optional<bool> mute;
    // Which GainControl should this command apply to?
    enum class Control { SOURCE, ADJUSTMENT };
    Control control = Control::SOURCE;
  };
  void PostStreamGainMute(StreamGainCommand gain_command);

  float stream_gain_db_ = media_audio::kUnityGainDb;
  bool mute_ = false;
  std::optional<Format> format_;

  fuchsia::media::AudioRenderUsage usage_ = fuchsia::media::AudioRenderUsage::MEDIA;

  // Set when pause is ramping, cleared when the ramp is finished.
  // Must be accessed on the FIDL thread only.
  struct PauseRampState {
    std::vector<PauseCallback> callbacks;  // Pause calls in flight
    std::vector<fit::closure> queued;      // closures to run on completion
  };
  std::shared_ptr<PauseRampState> pause_ramp_state_;
  void FinishPauseRamp(std::shared_ptr<PauseRampState> expected_state);
  zx::duration mix_profile_period_;

  std::mutex mutex_;
  bool reference_clock_is_set_ FXL_GUARDED_BY(mutex_) = false;
  std::optional<float> notified_gain_db_ FXL_GUARDED_BY(mutex_);
  std::optional<bool> notified_mute_ FXL_GUARDED_BY(mutex_);

  // Only used if kEnableDropoutChecks is set
  bool AnalyzePacket(fuchsia::media::StreamPacket packet);
  std::unique_ptr<PowerChecker> power_checker_;
  std::unique_ptr<SilenceChecker> silence_checker_;

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

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_RENDERER_H_
