// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_BASE_CAPTURER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_BASE_CAPTURER_H_

#include <fuchsia/media/audio/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/media/cpp/timeline_function.h>
#include <lib/media/cpp/timeline_rate.h>
#include <lib/zx/clock.h>

#include <atomic>
#include <memory>
#include <mutex>

#include <fbl/intrusive_double_list.h>

#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/context.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/mixer/output_producer.h"
#include "src/media/audio/audio_core/pending_capture_buffer.h"
#include "src/media/audio/audio_core/route_graph.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"
#include "src/media/audio/audio_core/usage_settings.h"
#include "src/media/audio/audio_core/utils.h"

namespace media::audio {

class BaseCapturer : public AudioObject, public fuchsia::media::AudioCapturer {
 protected:
  using RouteGraphRemover = void (RouteGraph::*)(const AudioObject&);
  BaseCapturer(std::optional<Format> format,
               fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request,
               Context* context, RouteGraphRemover remover = &RouteGraph::RemoveCapturer);

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

  bool has_pending_capture_buffers() {
    std::lock_guard<std::mutex> pending_lock(pending_lock_);
    return !pending_capture_buffers_.is_empty();
  }

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
  fit::result<std::shared_ptr<Mixer>, zx_status_t> InitializeSourceLink(
      const AudioObject& source, std::shared_ptr<Stream> stream) override;
  void CleanupSourceLink(const AudioObject& source, std::shared_ptr<Stream> stream) override;
  void OnLinkAdded() override;

 protected:
  const zx::clock& optimal_clock() const { return optimal_clock_; }
  const zx::clock& reference_clock() const { return reference_clock_; }
  void set_optimal_clock(zx::clock optimal_clock) { optimal_clock_ = std::move(optimal_clock); }
  void set_reference_clock(zx::clock ref_clock) { reference_clock_ = std::move(ref_clock); }

 private:
  void UpdateState(State new_state);

  void OverflowOccurred(FractionalFrames<int64_t> source_start, FractionalFrames<int64_t> mix_point,
                        zx::duration overflow_duration);
  void PartialOverflowOccurred(FractionalFrames<int64_t> source_offset, int64_t mix_offset);

  using PcbList = ::fbl::DoublyLinkedList<std::unique_ptr<PendingCaptureBuffer>>;

  void CreateOptimalReferenceClock();
  void EstablishDefaultReferenceClock();

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
  bool QueueNextAsyncPendingBuffer() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token())
      FXL_LOCKS_EXCLUDED(pending_lock_);
  void ShutdownFromMixDomain() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  // Thunk to send finished buffers back to the user, and to finish an async
  // mode stop operation.
  void FinishAsyncStopThunk() FXL_LOCKS_EXCLUDED(mix_domain_->token());
  void FinishBuffersThunk() FXL_LOCKS_EXCLUDED(mix_domain_->token());

  // Helper function used to return a set of pending capture buffers to a user.
  void FinishBuffers(const PcbList& finished_buffers) FXL_LOCKS_EXCLUDED(mix_domain_->token());

  fit::promise<> Cleanup() FXL_LOCKS_EXCLUDED(mix_domain_->token());
  void CleanupFromMixThread() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  void MixTimerThunk() {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, mix_domain_);
    Process();
  }

  void RecomputeMinFenceTime();

  void Shutdown(std::unique_ptr<BaseCapturer> self)
      FXL_LOCKS_EXCLUDED(context_.threading_model().FidlDomain().token());

  TimelineRate dest_frames_to_clock_mono_rate() {
    return TimelineRate(ZX_SEC(1), format_.frames_per_second());
  }
  TimelineRate fractional_dest_frames_to_clock_mono_rate() {
    return TimelineRate(ZX_SEC(1),
                        FractionalFrames<int64_t>(format_.frames_per_second()).raw_value());
  }

  fidl::Binding<fuchsia::media::AudioCapturer> binding_;
  Context& context_;
  ThreadingModel::OwnedDomainPtr mix_domain_;
  std::atomic<State> state_;
  zx::duration min_fence_time_;

  // Capture format and gain state.
  Format format_;
  uint32_t max_frames_per_capture_;

  // Shared buffer state
  fzl::VmoMapper payload_buf_;
  uint32_t payload_buf_frames_ = 0;

  WakeupEvent mix_wakeup_;
  async::TaskClosureMethod<BaseCapturer, &BaseCapturer::MixTimerThunk> mix_timer_
      FXL_GUARDED_BY(mix_domain_->token()){this};

  // Queues of capture buffers from the client: waiting to be filled, or waiting to be returned.
  std::mutex pending_lock_;
  PcbList pending_capture_buffers_ FXL_GUARDED_BY(pending_lock_);
  PcbList finished_capture_buffers_ FXL_GUARDED_BY(pending_lock_);

  // Intermediate mixing buffer and output producer
  std::unique_ptr<OutputProducer> output_producer_;

  std::vector<LinkMatrix::LinkHandle> source_links_ FXL_GUARDED_BY(mix_domain_->token());

  // Capture bookkeeping
  bool async_mode_ = false;
  fbl::RefPtr<VersionedTimelineFunction> clock_mono_to_fractional_dest_frames_ =
      fbl::MakeRefCounted<VersionedTimelineFunction>();
  int64_t frame_count_ FXL_GUARDED_BY(mix_domain_->token()) = 0;

  uint32_t async_frames_per_packet_;
  uint32_t async_next_frame_offset_ FXL_GUARDED_BY(mix_domain_->token()) = 0;
  StopAsyncCaptureCallback pending_async_stop_cbk_;

  // for glitch-debugging purposes
  std::atomic<uint16_t> overflow_count_;
  std::atomic<uint16_t> partial_overflow_count_;

  std::shared_ptr<MixStage> mix_stage_;

  RouteGraphRemover route_graph_remover_;

  // This clock is created and tuned by audio_core
  zx::clock optimal_clock_;

  // Whether default, optimal or custom clock, audio_core will treat this as not-rate-adjustable
  // (although if set to the optimal_clock_, tuning of that clock will be reflected here)
  zx::clock reference_clock_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_BASE_CAPTURER_H_
