// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_RENDERER_IMPL_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_RENDERER_IMPL_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>

#include <memory>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/media/timeline/timeline_function.h"
#include "src/media/audio/audio_core/audio_link_packet_source.h"
#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/audio_renderer_format_info.h"
#include "src/media/audio/audio_core/utils.h"

namespace media::audio {

class AudioCoreImpl;

class AudioRendererImpl
    : public AudioObject,
      public fbl::DoublyLinkedListable<fbl::RefPtr<AudioRendererImpl>>,
      public fuchsia::media::AudioRenderer,
      public fuchsia::media::audio::GainControl {
 public:
  static fbl::RefPtr<AudioRendererImpl> Create(
      fidl::InterfaceRequest<fuchsia::media::AudioRenderer>
          audio_renderer_request,
      AudioCoreImpl* owner);

  void Shutdown();
  void OnRenderRange(int64_t presentation_time, uint32_t duration){};
  void SnapshotCurrentTimelineFunction(int64_t reference_time,
                                       TimelineFunction* out,
                                       uint32_t* generation);

  void SetThrottleOutput(
      fbl::RefPtr<AudioLinkPacketSource> throttle_output_link);

  // Recompute the minimum clock lead time based on the current set of outputs
  // we are linked to.  If this requirement is different from the previous
  // requirement, report it to our users (if they care).
  void RecomputeMinClockLeadTime();

  // Note: format_info() is subject to change and must only be accessed from the
  // main message loop thread.  Outputs which are running on mixer threads
  // should never access format_info() directly from an AudioRenderer.  Instead,
  // they should use the format_info which was assigned to the AudioLink at the
  // time the link was created.
  const fbl::RefPtr<AudioRendererFormatInfo>& format_info() const {
    return format_info_;
  }
  bool format_info_valid() const { return (format_info_ != nullptr); }

  // AudioRenderer interface
  void SetPcmStreamType(fuchsia::media::AudioStreamType format) final;
  void SetStreamType(fuchsia::media::StreamType format) final;
  void AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) final;
  void RemovePayloadBuffer(uint32_t id) final;
  void SetPtsUnits(uint32_t tick_per_second_numerator,
                   uint32_t tick_per_second_denominator) final;
  void SetPtsContinuityThreshold(float threshold_seconds) final;
  void SetReferenceClock(zx::handle ref_clock) final;
  void SendPacket(fuchsia::media::StreamPacket packet,
                  SendPacketCallback callback) final;
  void SendPacketNoReply(fuchsia::media::StreamPacket packet) final;
  void EndOfStream() final;
  void DiscardAllPackets(DiscardAllPacketsCallback callback) final;
  void DiscardAllPacketsNoReply() final;
  void Play(int64_t reference_time, int64_t media_time,
            PlayCallback callback) final;
  void PlayNoReply(int64_t reference_time, int64_t media_time) final;
  void Pause(PauseCallback callback) final;
  void PauseNoReply() final;
  void BindGainControl(
      fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) final;
  void EnableMinLeadTimeEvents(bool enabled) final;
  void GetMinLeadTime(GetMinLeadTimeCallback callback) final;

  // GainControl interface.
  void SetGain(float gain_db) final;
  void SetGainWithRamp(float gain_db, zx_duration_t duration_ns,
                       fuchsia::media::audio::RampType ramp_type) final;
  void SetMute(bool muted) final;
  void NotifyGainMuteChanged();
  // TODO(mpuryear): Notify on SetGainWithRamp.
  // TODO(mpuryear): consider EnableGainChangeEvents(bool), like MinLeadTime.

 protected:
  // Hook called when the minimum clock lead time requirement changes.
  void ReportNewMinClockLeadTime();

  fbl::RefPtr<AudioRendererFormatInfo> format_info_;
  float stream_gain_db_ = 0.0;
  bool mute_ = false;
  fbl::RefPtr<AudioLinkPacketSource> throttle_output_link_;

  // Minimum Clock Lead Time state
  int64_t min_clock_lead_nsec_ = 0;

 private:
  class GainControlBinding : public fuchsia::media::audio::GainControl {
   public:
    static std::unique_ptr<GainControlBinding> Create(
        AudioRendererImpl* owner) {
      return std::unique_ptr<GainControlBinding>(new GainControlBinding(owner));
    }

    // GainControl interface.
    void SetGain(float gain_db) final;
    void SetGainWithRamp(float gain_db, zx_duration_t duration_ns,
                         fuchsia::media::audio::RampType ramp_type) final;
    void SetMute(bool muted) final;

   private:
    friend class std::default_delete<GainControlBinding>;

    GainControlBinding(AudioRendererImpl* owner) : owner_(owner) {}
    ~GainControlBinding() override {}

    AudioRendererImpl* owner_;
  };

  friend class fbl::RefPtr<AudioRendererImpl>;
  friend class GainControlBinding;

  AudioRendererImpl(fidl::InterfaceRequest<fuchsia::media::AudioRenderer>
                        audio_renderer_request,
                    AudioCoreImpl* owner);

  ~AudioRendererImpl() override;

  bool IsOperating();
  bool ValidateConfig();
  void ComputePtsToFracFrames(int64_t first_pts);
  void UnlinkThrottle();

  AudioCoreImpl* owner_ = nullptr;
  fidl::Binding<fuchsia::media::AudioRenderer> audio_renderer_binding_;
  fidl::BindingSet<fuchsia::media::audio::GainControl,
                   std::unique_ptr<GainControlBinding>>
      gain_control_bindings_;
  bool is_shutdown_ = false;
  fbl::RefPtr<RefCountedVmoMapper> payload_buffer_;
  bool config_validated_ = false;

  // PTS interpolation state.
  int64_t next_frac_frame_pts_ = 0;
  TimelineRate pts_ticks_per_second_;
  TimelineRate frac_frames_per_pts_tick_;
  TimelineFunction pts_to_frac_frames_;
  bool pts_to_frac_frames_valid_ = false;
  float pts_continuity_threshold_ = 0.0f;
  bool pts_continuity_threshold_set_ = false;
  int64_t pts_continuity_threshold_frac_frame_ = 0;

  // Play/Pause state
  int64_t pause_time_frac_frames_;
  bool pause_time_frac_frames_valid_ = false;
  TimelineRate frac_frames_per_ref_tick_;

  // Minimum Clock Lead Time state
  bool min_clock_lead_time_events_enabled_ = false;

  fbl::Mutex ref_to_ff_lock_;
  TimelineFunction ref_clock_to_frac_frames_ FXL_GUARDED_BY(ref_to_ff_lock_);
  GenerationId ref_clock_to_frac_frames_gen_ FXL_GUARDED_BY(ref_to_ff_lock_);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_RENDERER_IMPL_H_
