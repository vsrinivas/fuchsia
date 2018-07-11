// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_RENDERER_IMPL_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_RENDERER_IMPL_H_

#include <fbl/unique_ptr.h>
#include <fuchsia/media/cpp/fidl.h>

#include "garnet/bin/media/audio_server/audio_link_packet_source.h"
#include "garnet/bin/media/audio_server/audio_object.h"
#include "garnet/bin/media/audio_server/audio_renderer_format_info.h"
#include "garnet/bin/media/audio_server/utils.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/media/timeline/timeline_function.h"

namespace media {
namespace audio {

class AudioServerImpl;

class AudioRendererImpl
    : public AudioObject,
      public fbl::DoublyLinkedListable<fbl::RefPtr<AudioRendererImpl>>,
      public fuchsia::media::AudioRenderer2 {
 public:
  static fbl::RefPtr<AudioRendererImpl> Create(
      fidl::InterfaceRequest<fuchsia::media::AudioRenderer2>
          audio_renderer_request,
      AudioServerImpl* owner);

  void Shutdown();
  void OnRenderRange(int64_t presentation_time, uint32_t duration){};
  void SnapshotCurrentTimelineFunction(int64_t reference_time,
                                       TimelineFunction* out,
                                       uint32_t* generation);

  void SetThrottleOutput(
      std::shared_ptr<AudioLinkPacketSource> throttle_output_link);

  // Recompute the minimum clock lead time based on the current set of outputs
  // we are linked to.  If this requirement is different from the previous
  // requirement, report it to our users (if they care).
  void RecomputeMinClockLeadTime();

  // Note: format_info() is subject to change and must only be accessed from the
  // main message loop thread.  Outputs which are running on mixer threads
  // should never access format_info() directly from a renderer.  Instead, they
  // should use the format_info which was assigned to the AudioLink at the time
  // the link was created.
  const fbl::RefPtr<AudioRendererFormatInfo>& format_info() const {
    return format_info_;
  }
  bool format_info_valid() const { return (format_info_ != nullptr); }

  float db_gain() const { return db_gain_; }

  // AudioRenderer Interface
  void SetPcmFormat(fuchsia::media::AudioPcmFormat format) final;
  void SetPayloadBuffer(zx::vmo payload_buffer) final;
  void SetPtsUnits(uint32_t tick_per_second_numerator,
                   uint32_t tick_per_second_denominator) final;
  void SetPtsContinuityThreshold(float threshold_seconds) final;
  void SetReferenceClock(zx::handle ref_clock) final;
  void SendPacket(fuchsia::media::AudioPacket packet,
                  SendPacketCallback callback) final;
  void SendPacketNoReply(fuchsia::media::AudioPacket packet) final;
  void Flush(FlushCallback callback) final;
  void FlushNoReply() final;
  void Play(int64_t reference_time, int64_t media_time,
            PlayCallback callback) final;
  void PlayNoReply(int64_t reference_time, int64_t media_time) final;
  void Pause(PauseCallback callback) final;
  void PauseNoReply() final;
  void SetGainMute(float gain, bool mute, uint32_t flags,
                   SetGainMuteCallback callback) final;
  void SetGainMuteNoReply(float gain, bool mute, uint32_t flags) final;
  void DuplicateGainControlInterface(
      fidl::InterfaceRequest<fuchsia::media::AudioRendererGainControl> request)
      final;
  void EnableMinLeadTimeEvents(bool enabled) final;
  void GetMinLeadTime(GetMinLeadTimeCallback callback) final;

 protected:
  // Hook called when the minimum clock lead time requirement changes.
  void ReportNewMinClockLeadTime();

  fbl::RefPtr<AudioRendererFormatInfo> format_info_;
  float db_gain_ = 0.0;
  bool mute_ = false;
  std::shared_ptr<AudioLinkPacketSource> throttle_output_link_;

  // Minimum Clock Lead Time state
  int64_t min_clock_lead_nsec_ = 0;

 private:
  class GainControlBinding : public fuchsia::media::AudioRendererGainControl {
   public:
    static fbl::unique_ptr<GainControlBinding> Create(
        AudioRendererImpl* owner) {
      return fbl::unique_ptr<GainControlBinding>(new GainControlBinding(owner));
    }

    bool gain_events_enabled() const { return gain_events_enabled_; }

    // AudioRendererGainControl
    void SetGainMute(float gain, bool mute, uint32_t flags,
                     SetGainMuteCallback callback) override;
    void SetGainMuteNoReply(float gain, bool mute, uint32_t flags) override;

   private:
    friend class fbl::unique_ptr<GainControlBinding>;

    GainControlBinding(AudioRendererImpl* owner) : owner_(owner) {}
    ~GainControlBinding() override {}

    AudioRendererImpl* owner_;
    bool gain_events_enabled_ = false;
  };

  friend class fbl::RefPtr<AudioRendererImpl>;
  friend class GainControlBinding;

  AudioRendererImpl(fidl::InterfaceRequest<fuchsia::media::AudioRenderer2>
                        audio_renderer_request,
                    AudioServerImpl* owner);

  ~AudioRendererImpl() override;

  bool IsOperating();
  bool ValidateConfig();
  void ComputePtsToFracFrames(int64_t first_pts);

  AudioServerImpl* owner_ = nullptr;
  fidl::Binding<fuchsia::media::AudioRenderer2> audio_renderer_binding_;
  fidl::BindingSet<fuchsia::media::AudioRendererGainControl,
                   fbl::unique_ptr<GainControlBinding>>
      gain_control_bindings_;
  bool is_shutdown_ = false;
  bool gain_events_enabled_ = false;
  fbl::RefPtr<vmo_utils::RefCountedVmoMapper> payload_buffer_;
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

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_RENDERER_IMPL_H_
