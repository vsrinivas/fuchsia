// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_BASE_RENDERER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_BASE_RENDERER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/zx/clock.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "src/media/audio/audio_core/v1/audio_object.h"
#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/context.h"
#include "src/media/audio/audio_core/v1/link_matrix.h"
#include "src/media/audio/audio_core/v1/packet_queue.h"
#include "src/media/audio/audio_core/v1/reporter.h"
#include "src/media/audio/audio_core/v1/route_graph.h"
#include "src/media/audio/audio_core/v1/usage_settings.h"
#include "src/media/audio/audio_core/v1/utils.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/timeline/timeline_function.h"
#include "src/media/audio/lib/wav/wav_writer.h"

namespace media::audio {

constexpr bool kEnableRendererWavWriters = false;

class AudioAdmin;
class StreamRegistry;
class Reporter;

class BaseRenderer : public AudioObject,
                     public fuchsia::media::AudioRenderer,
                     public std::enable_shared_from_this<BaseRenderer> {
 public:
  ~BaseRenderer() override;

  void OnRenderRange(int64_t presentation_time, uint32_t duration) {}

  // |fuchsia::media::AudioRenderer|
  void AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) final;
  void RemovePayloadBuffer(uint32_t id) final;
  void SetPtsUnits(uint32_t tick_per_second_numerator, uint32_t tick_per_second_denominator) final;
  void SetPtsContinuityThreshold(float threshold_seconds) final;
  void GetReferenceClock(GetReferenceClockCallback callback) final;
  void SendPacket(fuchsia::media::StreamPacket packet, SendPacketCallback callback) final;
  void SendPacketNoReply(fuchsia::media::StreamPacket packet) final;
  void EndOfStream() final;
  void DiscardAllPackets(DiscardAllPacketsCallback callback) final;
  void DiscardAllPacketsNoReply() final;
  void EnableMinLeadTimeEvents(bool enabled) final;
  void GetMinLeadTime(GetMinLeadTimeCallback callback) final;
  void Play(int64_t reference_time, int64_t media_time, PlayCallback callback) final;
  void PlayNoReply(int64_t ref_time, int64_t med_time) final { Play(ref_time, med_time, nullptr); }
  void Pause(PauseCallback callback) final;
  void PauseNoReply() final { Pause(nullptr); }

  std::shared_ptr<Clock> reference_clock() { return clock_; }

 protected:
  BaseRenderer(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request,
               Context* context);

  Context& context() const { return context_; }

  // |media::audio::AudioObject|
  void OnLinkAdded() override;
  fpromise::result<std::shared_ptr<ReadableStream>, zx_status_t> InitializeDestLink(
      const AudioObject& dest) override;
  void CleanupDestLink(const AudioObject& dest) override;

  void ReportStartIfStopped();
  void ReportStopIfStarted();
  // Extensible by children, but the parent must also be called
  virtual void ReportStart();
  virtual void ReportStop();

  // Can be overridden by subclasses that need to wrap these methods.
  virtual void AddPayloadBufferInternal(uint32_t id, zx::vmo payload_buffer);
  virtual void RemovePayloadBufferInternal(uint32_t id);
  virtual void SendPacketInternal(fuchsia::media::StreamPacket packet, SendPacketCallback callback);
  virtual void DiscardAllPacketsInternal(DiscardAllPacketsCallback callback);
  virtual void EnableMinLeadTimeEventsInternal(bool enabled);
  virtual void GetMinLeadTimeInternal(GetMinLeadTimeCallback callback);
  virtual void PlayInternal(zx::time reference_time, zx::time media_time, PlayCallback callback);
  virtual void PauseInternal(PauseCallback callback);

  // Hook called when the minimum clock lead time requirement changes.
  void ReportNewMinLeadTime();

  bool IsOperating();
  bool IsPlaying() const { return state_ == State::Playing; }

  void InvalidateConfiguration() { config_validated_ = false; }

  // Minimum Lead Time state
  zx::duration min_lead_time_;

  fidl::Binding<fuchsia::media::AudioRenderer>& binding() { return audio_renderer_binding_; }

  zx_status_t SetAdjustableReferenceClock();
  zx_status_t SetCustomReferenceClock(zx::clock ref_clock);
  Reporter::Renderer& reporter() { return *reporter_; }

  // Only needed by AudioRenderer if glitch-/dropout-detection is enabled.
  std::unordered_map<uint32_t, fbl::RefPtr<RefCountedVmoMapper>> payload_buffers() {
    return payload_buffers_;
  }
  int64_t frames_received() const { return frames_received_; }

 private:
  static constexpr uint32_t kDefaultPtsTicksPerSecondNumerator = 1'000'000'000;
  static constexpr uint32_t kDefaultPtsTicksPerSecondDenominator = 1;

  // Recompute the minimum clock lead time based on the current set of outputs
  // we are linked to.  If this requirement is different from the previous
  // requirement, report it to our users (if they care).
  void RecomputeMinLeadTime();

  bool ValidateConfig();
  void ComputePtsToFracFrames(int64_t first_pts);

  Context& context_;
  fidl::Binding<fuchsia::media::AudioRenderer> audio_renderer_binding_;

  std::unordered_map<uint32_t, fbl::RefPtr<RefCountedVmoMapper>> payload_buffers_;
  bool config_validated_ = false;

  // PTS interpolation state.
  Fixed next_frac_frame_pts_{0};
  TimelineRate pts_ticks_per_second_;
  TimelineRate frac_frames_per_pts_tick_;
  TimelineFunction pts_to_frac_frames_;
  bool pts_to_frac_frames_valid_ = false;
  float pts_continuity_threshold_ = 0.0f;
  bool pts_continuity_threshold_set_ = false;
  Fixed pts_continuity_threshold_frac_frame_{0};
  int64_t frames_received_ = 0;

  // Play/Pause state
  Fixed pause_time_frac_frames_;
  bool pause_time_frac_frames_valid_ = false;
  TimelineRate frac_frames_per_ref_tick_;

  enum class State { Playing, Paused };
  State state_ = State::Paused;

  std::optional<zx::time> pause_reference_time_ = std::nullopt;
  std::optional<zx::time> pause_media_time_ = std::nullopt;

  // Minimum Clock Lead Time state
  bool min_lead_time_events_enabled_ = false;

  fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames_;

  std::unordered_map<const AudioObject*, std::shared_ptr<PacketQueue>> packet_queues_;
  Packet::Allocator packet_allocator_;

  WavWriter<kEnableRendererWavWriters> wav_writer_;
  Reporter::Container<Reporter::Renderer, Reporter::kObjectsToCache>::Ptr reporter_;

  std::shared_ptr<Clock> clock_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_BASE_RENDERER_H_
