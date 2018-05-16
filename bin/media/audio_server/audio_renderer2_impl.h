// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <fbl/vmo_mapper.h>

#include <media/cpp/fidl.h>
#include "garnet/bin/media/audio_server/audio_object.h"
#include "garnet/bin/media/audio_server/audio_renderer_impl.h"
#include "garnet/bin/media/audio_server/utils.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"

namespace media {
namespace audio {

class AudioServerImpl;

class AudioRenderer2Impl : public AudioRendererImpl, public AudioRenderer2 {
 public:
  static fbl::RefPtr<AudioRenderer2Impl> Create(
      fidl::InterfaceRequest<AudioRenderer2> audio_renderer_request,
      AudioServerImpl* owner);

  // AudioRendererImpl implementation
  // TODO(johngro) : Collapse AudioRendererImpl into AudioRenderer2Impl when
  // AudioRenderer1Impl has been fully depricated and removed.
  void Shutdown() override;
  void OnRenderRange(int64_t presentation_time, uint32_t duration) override{};
  void SnapshotCurrentTimelineFunction(int64_t reference_time,
                                       TimelineFunction* out,
                                       uint32_t* generation) override;

  // AudioRenderer2 Interface
  void SetPcmFormat(AudioPcmFormat format) final;
  void SetPayloadBuffer(zx::vmo payload_buffer) final;
  void SetPtsUnits(uint32_t tick_per_second_numerator,
                   uint32_t tick_per_second_denominator) final;
  void SetPtsContinuityThreshold(float threshold_seconds) final;
  void SetReferenceClock(zx::handle ref_clock) final;
  void SendPacket(AudioPacket packet, SendPacketCallback callback) final;
  void SendPacketNoReply(AudioPacket packet) final;
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
      fidl::InterfaceRequest<AudioRendererGainControl> request) final;
  void EnableMinLeadTimeEvents(bool enabled) final;
  void GetMinLeadTime(GetMinLeadTimeCallback callback) final;

 protected:
  // Hook called when the minimum clock lead time requirement changes.
  void ReportNewMinClockLeadTime() final;

 private:
  // TODO(johngro): When AudioPipe is fully retired, eliminate the V1/V2
  // versions of audio packet refs, and fold this definition into the global
  // AudioPacketRef definition (eliminating all of the virtual functions as we
  // go).
  class AudioPacketRefV2 : public ::media::audio::AudioPacketRef {
   public:
    void Cleanup() final {
      FXL_DCHECK(callback_ != nullptr);
      callback_();
    }

    void* payload() final {
      auto start = reinterpret_cast<uint8_t*>(vmo_ref_->start());
      return (start + packet_.payload_offset);
    }

    uint32_t flags() final { return packet_.flags; }

    AudioPacketRefV2(fbl::RefPtr<fbl::RefCountedVmoMapper> vmo_ref,
                     AudioRenderer2::SendPacketCallback callback,
                     AudioPacket packet, AudioServerImpl* server,
                     uint32_t frac_frame_len, int64_t start_pts);

   protected:
    bool NeedsCleanup() final { return callback_ != nullptr; }

    fbl::RefPtr<fbl::RefCountedVmoMapper> vmo_ref_;
    AudioRenderer2::SendPacketCallback callback_;
    AudioPacket packet_;
  };

  class GainControlBinding : public AudioRendererGainControl {
   public:
    static fbl::unique_ptr<GainControlBinding> Create(
        AudioRenderer2Impl* owner) {
      return fbl::unique_ptr<GainControlBinding>(new GainControlBinding(owner));
    }

    bool gain_events_enabled() const { return gain_events_enabled_; }

    // AudioRendererGainControl
    void SetGainMute(float gain, bool mute, uint32_t flags,
                     SetGainMuteCallback callback) override;
    void SetGainMuteNoReply(float gain, bool mute, uint32_t flags) override;

   private:
    friend class fbl::unique_ptr<GainControlBinding>;

    GainControlBinding(AudioRenderer2Impl* owner) : owner_(owner) {}
    ~GainControlBinding() override {}

    AudioRenderer2Impl* owner_;
    bool gain_events_enabled_ = false;
  };

  friend class fbl::RefPtr<AudioRenderer2Impl>;
  friend class GainControlBinding;

  AudioRenderer2Impl(
      fidl::InterfaceRequest<AudioRenderer2> audio_renderer_request,
      AudioServerImpl* owner);

  ~AudioRenderer2Impl() override;

  bool IsOperating();
  bool ValidateConfig();
  void ComputePtsToFracFrames(int64_t first_pts);

  AudioServerImpl* owner_ = nullptr;
  fidl::Binding<AudioRenderer2> audio_renderer_binding_;
  fidl::BindingSet<AudioRendererGainControl,
                   fbl::unique_ptr<GainControlBinding>>
      gain_control_bindings_;
  bool is_shutdown_ = false;
  bool gain_events_enabled_ = false;
  fbl::RefPtr<fbl::RefCountedVmoMapper> payload_buffer_;
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
