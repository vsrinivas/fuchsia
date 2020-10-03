// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_BASE_CAPTURER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_BASE_CAPTURER_H_

#include <fuchsia/media/audio/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/clock.h>

#include <atomic>
#include <memory>
#include <mutex>

#include <fbl/intrusive_double_list.h>

#include "src/media/audio/audio_core/audio_clock.h"
#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/capture_packet_queue.h"
#include "src/media/audio/audio_core/context.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/mixer/output_producer.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/audio_core/route_graph.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"
#include "src/media/audio/audio_core/usage_settings.h"
#include "src/media/audio/audio_core/utils.h"
#include "src/media/audio/lib/timeline/timeline_function.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media::audio {

class BaseCapturer : public AudioObject,
                     public fuchsia::media::AudioCapturer,
                     public std::enable_shared_from_this<BaseCapturer> {
 public:
  AudioClock& reference_clock() { return audio_clock_; }

  // TODO(fxbug.dev/43507): This is a temporary flag to ease the transition. This will be exposed as
  // a command line flag for audio_core. This has no effect in DynamicallyAllocated mode.
  //
  // When false (the default), packets are automatically recycled after each call to Push.
  // This gives equivalent behavior to the "current" code, i.e., before the bug fix.
  // Otherwise, packets must be explicitly recycle.
  //
  // Eventually this flag will be deleted and the behavior will be hardcoded to "true".
  static void SetMustReleasePackets(bool b) { must_release_packets_ = b; }

 protected:
  using RouteGraphRemover = void (RouteGraph::*)(const AudioObject&);
  BaseCapturer(std::optional<Format> format,
               fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request,
               Context* context);

  ~BaseCapturer() override;

  Context& context() const { return context_; }

  ExecutionDomain& mix_domain() const { return *mix_domain_; }

  // Notes about the BaseCapturer state machine.
  //
  // :: WaitingForVmo ::
  // AudioCapturers start in this mode. They should have a default capture mode
  // set, and will accept a mode change up until the point where they have a
  // shared payload VMO assigned to them. At this point they transition into the
  // OperatingSync state. Only the main service thread may transition out of
  // this state.
  //
  // :: OperatingSync ::
  // After a mode has been assigned and a shared payload VMO has provided, the
  // AudioCapturer is now operating in synchronous mode. Clients may provided
  // buffers to be filled using the CaptureAt method and may cancel these
  // buffers using the Flush method. They may also transition to asynchronous
  // mode by calling StartAsyncCapture, but only when there are no pending
  // buffers in flight. Only the main service thread may transition out of
  // this state.
  //
  // :: OperatingAsync ::
  // AudioCapturers enter OperatingAsync after a successful call to
  // StartAsyncCapture. Threads from the mix_domain allocate and fill pending
  // payload buffers, then signal the main service thread in order to send them
  // back to the client over the AudioCapturerClient interface provided when
  // starting. CaptureAt and Flush are illegal operations while in this state.
  // clients may begin the process of returning to synchronous capture mode by
  // calling StopAsyncCapture. Only the main service thread may transition out
  // of this state.
  //
  // :: AsyncStopping ::
  // AudioCapturers enter AsyncStopping after a successful call to
  // StopAsyncCapture. A thread from the mix_domain will handle the details of
  // stopping, including transferring all partially filled pending buffers to
  // the finished queue. Aside from setting the gain, all operations are illegal
  // while the AudioCapturer is in the process of stopping. Once the mix domain
  // thread has finished cleaning up, it will transition to the
  // AsyncStoppingCallbackPending state and signal the main service thread in
  // order to complete the process. Only a mix domain thread may transition out
  // of this state.
  //
  // :: AsyncStoppingCallbackPending ::
  // AudioCapturers enter AsyncStoppingCallbackPending after a mix domain thread
  // has finished the process of shutting down the capture process and is ready
  // to signal to the client that the AudioCapturer is now in synchronous
  // capture mode again. The main service thread will send all partially and
  // completely filled buffers to the user, ensuring that there is at least one
  // buffer sent indicating end-of-stream, even if that buffer needs to be of
  // zero length. Finally, the main service thread will signal that the stopping
  // process is finished using the client supplied callback (if any), and
  // finally transition back to the OperatingSync state.
  enum class State {
    WaitingForVmo,
    OperatingSync,
    OperatingAsync,
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

  virtual void ReportStart() {}
  virtual void ReportStop() {}
  virtual void OnStateChanged(State old_state, State new_stage);
  virtual void SetRoutingProfile(bool routable) = 0;

  static bool StateIsRoutable(BaseCapturer::State state) {
    return state != BaseCapturer::State::WaitingForVmo && state != BaseCapturer::State::Shutdown;
  }

  // |media::audio::AudioObject|
  fit::result<std::pair<std::shared_ptr<Mixer>, ExecutionDomain*>, zx_status_t>
  InitializeSourceLink(const AudioObject& source, std::shared_ptr<ReadableStream> stream) override;
  void CleanupSourceLink(const AudioObject& source,
                         std::shared_ptr<ReadableStream> stream) override;
  void OnLinkAdded() override;

  fidl::Binding<fuchsia::media::AudioCapturer>& binding() { return binding_; }

  void SetAdjustableReferenceClock();

  // AudioCore treats client-provided clocks as not-rate-adjustable.
  void SetClock(AudioClock audio_clock) { audio_clock_ = std::move(audio_clock); }

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

  fit::promise<> Cleanup() FXL_LOCKS_EXCLUDED(mix_domain_->token());
  void CleanupFromMixThread() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  void MixTimerThunk() {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, mix_domain_);
    Process();
  }

  void RecomputePresentationDelay();

  TimelineRate dest_frames_to_ref_clock_rate() {
    return TimelineRate(ZX_SEC(1), format_.frames_per_second());
  }

  fidl::Binding<fuchsia::media::AudioCapturer> binding_;
  Context& context_;
  ThreadingModel::OwnedDomainPtr mix_domain_;
  std::atomic<State> state_;
  zx::duration presentation_delay_;

  // Capture format and gain state.
  Format format_;
  uint32_t max_frames_per_capture_;

  // Shared buffer state
  fzl::VmoMapper payload_buf_;

  WakeupEvent mix_wakeup_;
  WakeupEvent ready_packets_wakeup_;
  async::TaskClosureMethod<BaseCapturer, &BaseCapturer::MixTimerThunk> mix_timer_
      FXL_GUARDED_BY(mix_domain_->token()){this};

  // Queue of pending and ready packets.
  //
  // Concurrency notes: Initially this is nullptr. When we transition to state OperatingSync
  // or OperatingAsync, we create a new queue and start mixing. Later, in response to a FIDL
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
  int64_t frame_pointer_ FXL_GUARDED_BY(mix_domain_->token()) = 0;
  uint64_t overflow_count_ FXL_GUARDED_BY(mix_domain_->token()) = 0;

  StopAsyncCaptureCallback pending_async_stop_cbk_;

  std::shared_ptr<MixStage> mix_stage_;
  Reporter::Container<Reporter::Capturer>::Ptr reporter_;

  AudioClock audio_clock_;

  // TODO(fxbug.dev/43507): This is a temporary flag.
  static bool must_release_packets_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_BASE_CAPTURER_H_
