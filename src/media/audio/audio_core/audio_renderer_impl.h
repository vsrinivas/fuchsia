// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_RENDERER_IMPL_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_RENDERER_IMPL_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/media/cpp/timeline_function.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/format.h"
#include "src/media/audio/audio_core/packet_queue.h"
#include "src/media/audio/audio_core/route_graph.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"
#include "src/media/audio/audio_core/utils.h"
#include "src/media/audio/lib/wav_writer/wav_writer.h"

namespace media::audio {

constexpr bool kEnableRendererWavWriters = false;

class AudioAdmin;
class AudioCoreImpl;
class StreamRegistry;

class AudioRendererImpl : public AudioObject,
                          public fuchsia::media::AudioRenderer,
                          public fuchsia::media::audio::GainControl,
                          public StreamVolume {
 public:
  static fbl::RefPtr<AudioRendererImpl> Create(
      fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request,
      async_dispatcher_t* dispatcher, RouteGraph* route_graph, AudioAdmin* admin,
      fbl::RefPtr<fzl::VmarManager> vmar, StreamVolumeManager* volume_manager);
  static fbl::RefPtr<AudioRendererImpl> Create(
      fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request,
      AudioCoreImpl* owner);

  void Shutdown();
  void OnRenderRange(int64_t presentation_time, uint32_t duration){};

  // |fuchsia::media::AudioRenderer|
  void SetPcmStreamType(fuchsia::media::AudioStreamType format) final;
  void AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) final;
  void RemovePayloadBuffer(uint32_t id) final;
  void SetPtsUnits(uint32_t tick_per_second_numerator, uint32_t tick_per_second_denominator) final;
  void SetPtsContinuityThreshold(float threshold_seconds) final;
  void SetReferenceClock(zx::handle ref_clock) final;
  void SendPacket(fuchsia::media::StreamPacket packet, SendPacketCallback callback) final;
  void SendPacketNoReply(fuchsia::media::StreamPacket packet) final;
  void EndOfStream() final;
  void DiscardAllPackets(DiscardAllPacketsCallback callback) final;
  void DiscardAllPacketsNoReply() final;
  void Play(int64_t reference_time, int64_t media_time, PlayCallback callback) final;
  void PlayNoReply(int64_t reference_time, int64_t media_time) final;
  void Pause(PauseCallback callback) final;
  void PauseNoReply() final;
  void BindGainControl(fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) final;
  void EnableMinLeadTimeEvents(bool enabled) final;
  void GetMinLeadTime(GetMinLeadTimeCallback callback) final;
  void SetUsage(fuchsia::media::AudioRenderUsage usage) override;

  // |fuchsia::media::audio::GainControl|
  void SetGain(float gain_db) final;
  void SetGainWithRamp(float gain_db, int64_t duration_ns,
                       fuchsia::media::audio::RampType ramp_type) final;
  void SetMute(bool muted) final;
  void NotifyGainMuteChanged();
  // TODO(mpuryear): Notify on SetGainWithRamp.
  // TODO(mpuryear): consider EnableGainChangeEvents(bool), like MinLeadTime.

 protected:
  // Hook called when the minimum clock lead time requirement changes.
  void ReportNewMinLeadTime();

  fbl::RefPtr<Format> format_;

  fuchsia::media::AudioRenderUsage usage_ = fuchsia::media::AudioRenderUsage::MEDIA;

  float stream_gain_db_ = 0.0;
  bool mute_ = false;

  // Minimum Lead Time state
  zx::duration min_lead_time_;

 private:
  class GainControlBinding : public fuchsia::media::audio::GainControl {
   public:
    static std::unique_ptr<GainControlBinding> Create(AudioRendererImpl* owner) {
      return std::unique_ptr<GainControlBinding>(new GainControlBinding(owner));
    }

    // |fuchsia::media::audio::GainControl|
    void SetGain(float gain_db) final;
    void SetGainWithRamp(float gain_db, int64_t duration_ns,
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

  AudioRendererImpl(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request,
                    async_dispatcher_t* dispatcher, RouteGraph* route_graph, AudioAdmin* admin,
                    fbl::RefPtr<fzl::VmarManager> vmar, StreamVolumeManager* volume_manager);

  ~AudioRendererImpl() override;

  // Recompute the minimum clock lead time based on the current set of outputs
  // we are linked to.  If this requirement is different from the previous
  // requirement, report it to our users (if they care).
  void RecomputeMinLeadTime();

  bool IsOperating();
  bool ValidateConfig();
  void ComputePtsToFracFrames(int64_t first_pts);

  void ReportStart();
  void ReportStop();

  // |media::audio::AudioObject|
  void OnLinkAdded() override;
  const fbl::RefPtr<Format>& format() const final { return format_; }
  zx_status_t InitializeDestLink(const fbl::RefPtr<AudioLink>& link) override;
  void CleanupDestLink(const fbl::RefPtr<AudioLink>& link) override;

  // |media::audio::StreamVolume|
  bool GetStreamMute() const final;
  fuchsia::media::Usage GetStreamUsage() const final;
  void RealizeVolume(VolumeCommand volume_command) final;

  async_dispatcher_t* dispatcher_;
  RouteGraph& route_graph_;
  AudioAdmin& admin_;
  fbl::RefPtr<fzl::VmarManager> vmar_;
  StreamVolumeManager& volume_manager_;

  fidl::Binding<fuchsia::media::AudioRenderer> audio_renderer_binding_;
  fidl::BindingSet<fuchsia::media::audio::GainControl, std::unique_ptr<GainControlBinding>>
      gain_control_bindings_;
  bool is_shutdown_ = false;
  std::unordered_map<uint32_t, fbl::RefPtr<RefCountedVmoMapper>> payload_buffers_;
  bool config_validated_ = false;

  // PTS interpolation state.
  FractionalFrames<int64_t> next_frac_frame_pts_{0};
  TimelineRate pts_ticks_per_second_;
  TimelineRate frac_frames_per_pts_tick_;
  TimelineFunction pts_to_frac_frames_;
  bool pts_to_frac_frames_valid_ = false;
  float pts_continuity_threshold_ = 0.0f;
  bool pts_continuity_threshold_set_ = false;
  FractionalFrames<int64_t> pts_continuity_threshold_frac_frame_{0};

  // Play/Pause state
  FractionalFrames<int64_t> pause_time_frac_frames_;
  bool pause_time_frac_frames_valid_ = false;
  TimelineRate frac_frames_per_ref_tick_;

  // Minimum Clock Lead Time state
  bool min_lead_time_events_enabled_ = false;

  fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames_;

  std::unordered_map<AudioLink*, fbl::RefPtr<PacketQueue>> packet_queues_;

  WavWriter<kEnableRendererWavWriters> wav_writer_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_RENDERER_IMPL_H_
