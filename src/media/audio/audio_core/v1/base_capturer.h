// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_BASE_CAPTURER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_BASE_CAPTURER_H_

#include <fuchsia/media/audio/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/clock.h>

#include <atomic>
#include <memory>
#include <mutex>

#include <fbl/intrusive_double_list.h>

#include "src/media/audio/audio_core/shared/mixer/mixer.h"
#include "src/media/audio/audio_core/shared/mixer/output_producer.h"
#include "src/media/audio/audio_core/v1/audio_object.h"
#include "src/media/audio/audio_core/v1/capture_packet_queue.h"
#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/context.h"
#include "src/media/audio/audio_core/v1/reporter.h"
#include "src/media/audio/audio_core/v1/route_graph.h"
#include "src/media/audio/audio_core/v1/stream_volume_manager.h"
#include "src/media/audio/audio_core/v1/usage_settings.h"
#include "src/media/audio/audio_core/v1/utils.h"
#include "src/media/audio/lib/timeline/timeline_function.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media::audio {

class BaseCapturer : public AudioObject,
                     public fuchsia::media::AudioCapturer,
                     public std::enable_shared_from_this<BaseCapturer> {
 public:
  std::shared_ptr<Clock> reference_clock() { return audio_clock_; }

 protected:
  using RouteGraphRemover = void (RouteGraph::*)(const AudioObject&);
  BaseCapturer(std::optional<Format> format,
               fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request,
               Context* context);

  ~BaseCapturer() override;

  Context& context() const { return context_; }

  ExecutionDomain& mix_domain() const { return *mix_domain_; }

  // The BaseCapturer state machine:
  //
  //                           (start)
  //                              |
  //                              V
  //                        WaitingForVmo
  //                              |
  //                              | (client provides a VMO)
  //                              V
  //                       WaitingForRequest
  //                     | ^              ^  |
  //                     | |              |  | (client calls CaptureAt)
  //                     | |  ( no more ) |  |
  //                     | |  (CaptureAt) |  |
  //                     | |  ( pending ) |  V
  //                     | |      SyncOperating
  //                     | |
  // (client calls     ) | |
  // (StartAsyncCapture) | +------------------+
  //                     V                    |
  //            AsyncOperating                |
  //                     |                    |
  //  (client calls    ) |                    |
  //  (StopAsyncCapture) |                    |
  //                     V                    |
  //            AsyncStopping                 |
  //                     |                    |
  //  (mixer thread    ) |                    |
  //  (finishes cleanup) |                    |
  //                     V                    |
  //            AsyncStoppingCallbackPending  |
  //                     |                    |
  // (FIDL thread      ) |                    |
  // (delivers callback) +--------------------+
  //
  //
  // :: WaitingForVmo ::
  // AudioCapturers start in this state. They should have a default capture
  // format set, and will accept a state change up until the point where they
  // have a shared payload VMO assigned to them.
  //
  // :: WaitingForRequest ::
  // After a format has been assigned and a shared payload VMO has provided, the
  // AudioCapturer is waiting to operate in Sync or Async mode.
  //
  // :: SyncOperating ::
  // AudioCapturers enter SyncOperating after a successful call to CaptureAt.
  // They remain in this state until all pending CaptureAt requests are handled
  // or flushed.
  //
  // :: AsyncOperating ::
  // AudioCapturers enter AsyncOperating after a successful call to
  // StartAsyncCapture. CaptureAt and Flush are illegal while in this state.
  //
  // :: AsyncStopping ::
  // :: AsyncStoppingCallbackPending ::
  // AudioCapturers enter AsyncStopping after a successful call to
  // StopAsyncCapture. A thread from the mix_domain will handle the details of
  // stopping. Aside from setting the gain, all operations are illegal while the
  // AudioCapturer is in the process of stopping. Once the mix domain thread has
  // finished cleaning up, it will transition to the AsyncStoppingCallbackPending
  // state and signal the main service thread in order to complete the process.
  //
  // :: Shutdown ::
  // AudioCapturers enter this state when the connection is closing. We might
  // transition to this state from any other state.
  enum class State {
    WaitingForVmo,
    WaitingForRequest,
    SyncOperating,
    AsyncOperating,
    AsyncStopping,
    AsyncStoppingCallbackPending,
    Shutdown,
  };
  State capture_state() const { return state_.load(); }

  bool has_pending_packets() {
    auto pq = packet_queue();
    return pq && pq->PendingSize() > 0;
  }
  bool IsOperating();

  void UpdateFormat(Format format) FXL_LOCKS_EXCLUDED(mix_domain_->token());

  // Removes the capturer from its owner, the route graph, triggering shutdown and drop.
  void BeginShutdown();

  virtual void ReportStart();
  virtual void ReportStop();
  virtual void OnStateChanged(State old_state, State new_stage);
  virtual void SetRoutingProfile(bool routable) = 0;

  static bool StateIsRoutable(BaseCapturer::State state) {
    return state != BaseCapturer::State::WaitingForVmo && state != BaseCapturer::State::Shutdown;
  }

  // |media::audio::AudioObject|
  fpromise::result<std::pair<std::shared_ptr<Mixer>, ExecutionDomain*>, zx_status_t>
  InitializeSourceLink(const AudioObject& source, std::shared_ptr<ReadableStream> stream) override;
  void CleanupSourceLink(const AudioObject& source,
                         std::shared_ptr<ReadableStream> stream) override;
  void OnLinkAdded() override;

  fidl::Binding<fuchsia::media::AudioCapturer>& binding() { return binding_; }

  // AudioCore treats client-provided clocks as not-rate-adjustable.
  void SetClock(std::shared_ptr<Clock> audio_clock) { audio_clock_ = std::move(audio_clock); }

  Reporter::Capturer& reporter() { return *reporter_; }

 private:
  void UpdateState(State new_state);

  // |fuchsia::media::AudioCapturer|
  void GetStreamType(GetStreamTypeCallback cbk) final;
  void AddPayloadBuffer(uint32_t id, zx::vmo payload_buf_vmo) final;
  void RemovePayloadBuffer(uint32_t id) final;
  void GetReferenceClock(GetReferenceClockCallback callback) final;
  void CaptureAt(uint32_t payload_buffer_id, uint32_t offset_frames, uint32_t num_frames,
                 CaptureAtCallback cbk) final;
  void ReleasePacket(fuchsia::media::StreamPacket packet) final;
  void DiscardAllPackets(DiscardAllPacketsCallback cbk) final;
  void DiscardAllPacketsNoReply() final;
  void StartAsyncCapture(uint32_t frames_per_packet) final;
  void StopAsyncCapture(StopAsyncCaptureCallback cbk) final;
  void StopAsyncCaptureNoReply() final;

  // Methods used by capture/mixer thread(s). Must be called from mix_domain.
  zx_status_t Process() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  void DoStopAsyncCapture() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  void ShutdownFromMixDomain() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  void ReportOverflow(zx::time start_time, zx::time end_time)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  // Thunk to send ready packets back to the user, and to finish an async
  // mode stop operation.
  void FinishAsyncStopThunk() FXL_LOCKS_EXCLUDED(mix_domain_->token());
  void FinishBuffersThunk() FXL_LOCKS_EXCLUDED(mix_domain_->token());

  // Helper function used to return a set of ready packets to a user.
  void FinishBuffers() FXL_LOCKS_EXCLUDED(mix_domain_->token());

  fpromise::promise<> Cleanup() FXL_LOCKS_EXCLUDED(mix_domain_->token());
  void CleanupFromMixThread() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  void MixTimerThunk() {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, mix_domain_);
    Process();
  }

  void RecomputePresentationDelay();

  TimelineRate dest_frames_to_ref_clock_rate() {
    return TimelineRate(ZX_SEC(1), format_->frames_per_second());
  }

  fidl::Binding<fuchsia::media::AudioCapturer> binding_;
  Context& context_;
  ThreadingModel::OwnedDomainPtr mix_domain_;
  std::atomic<State> state_;
  zx::duration presentation_delay_;

  // Capture format and gain state.
  std::optional<Format> format_;
  uint32_t max_frames_per_capture_;

  // Shared buffer state
  fzl::VmoMapper payload_buf_;

  WakeupEvent mix_wakeup_;
  WakeupEvent ready_packets_wakeup_;
  async::TaskClosureMethod<BaseCapturer, &BaseCapturer::MixTimerThunk> mix_timer_
      FXL_GUARDED_BY(mix_domain_->token()){this};

  // Queue of pending and ready packets.
  //
  // Concurrency notes: Initially this is nullptr. When we transition to state SyncOperating
  // or AsyncOperating, we create a new queue and start mixing. Later, in response to a FIDL
  // call, we might change operating modes from Sync -> Async or visa versa. To do this, we
  // create a new queue, but as this happens, the mixer may be concurrently mixing on the old
  // queue. We use a shared_ptr to ensure that the mixer can hold a valid reference for the
  // duration of the mix operation, even in the presence of a concurrent mode switch.
  //
  // To illustrate:
  //
  //   fidl_thread {
  //     // Switch from sync -> async.
  //     packet_queue()->DiscardPendingPackets();
  //     set_packet_queue_(CapturePacketQueue::CreatePreallocated(...));
  //   }
  //   mixer_thread {
  //     auto pq = packet_queue();
  //     auto state = pq->NextMixerJob();
  //     // ... FIDL thread might run here ...
  //     auto status = pq->FinishMixerJob(state);
  //     // ... will have status == Discarded ...
  //   }
  //
  std::mutex packet_queue_lock_;
  std::shared_ptr<CapturePacketQueue> packet_queue_ FXL_GUARDED_BY(packet_queue_lock_);

  std::shared_ptr<CapturePacketQueue> packet_queue() {
    std::lock_guard<std::mutex> lock(packet_queue_lock_);
    return packet_queue_;
  }
  void set_packet_queue(std::shared_ptr<CapturePacketQueue> pq) {
    std::lock_guard<std::mutex> lock(packet_queue_lock_);
    packet_queue_ = pq;
  }

  // Intermediate mixing buffer and output producer
  std::unique_ptr<OutputProducer> output_producer_;

  std::vector<LinkMatrix::LinkHandle> source_links_ FXL_GUARDED_BY(mix_domain_->token());

  // Capture bookkeeping
  fbl::RefPtr<VersionedTimelineFunction> ref_pts_to_fractional_frame_ =
      fbl::MakeRefCounted<VersionedTimelineFunction>();
  bool discontinuity_ FXL_GUARDED_BY(mix_domain_->token()) = true;
  // Running frame number in the capture stream.
  int64_t frame_pointer_ FXL_GUARDED_BY(mix_domain_->token()) = 0;
  uint64_t overflow_count_ FXL_GUARDED_BY(mix_domain_->token()) = 0;

  StopAsyncCaptureCallback pending_async_stop_cbk_;

  std::shared_ptr<MixStage> mix_stage_;
  Reporter::Container<Reporter::Capturer, Reporter::kObjectsToCache>::Ptr reporter_;

  std::shared_ptr<Clock> audio_clock_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_BASE_CAPTURER_H_
