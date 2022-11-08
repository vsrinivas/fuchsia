// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/base_capturer.h"

#include <lib/fit/defer.h>
#include <lib/fpromise/bridge.h>
#include <lib/media/audio/cpp/types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/errors.h>

#include <iomanip>
#include <memory>

#include "src/media/audio/audio_core/shared/audio_admin.h"
#include "src/media/audio/audio_core/v1/audio_core_impl.h"
#include "src/media/audio/audio_core/v1/audio_driver.h"
#include "src/media/audio/audio_core/v1/logging_flags.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/utils.h"

namespace media::audio {
namespace {

// Currently, the time we spend mixing must also be taken into account when reasoning about the
// capture presentation delay. Today (before any attempt at optimization), a particularly heavy mix
// pass may take longer than 1.5 msec on a DEBUG build(!) on relevant hardware. The constant below
// accounts for this, with additional padding for safety.
//
// TODO(fxbug.dev/91258): increase this, to account for worst-case cross-clock rate mismatches and
// mixes that may take longer than 1.5 msec.
const zx::duration kPresentationDelayPadding = zx::msec(3);

constexpr int64_t kMaxTimePerCapture = ZX_MSEC(50);

const Format kInitialFormat =
    Format::Create({.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                    .channels = 1,
                    .frames_per_second = 8000})
        .take_value();

}  // namespace

BaseCapturer::BaseCapturer(
    std::optional<Format> format,
    fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request, Context* context)
    : AudioObject(Type::AudioCapturer),
      binding_(this, std::move(audio_capturer_request)),
      context_(*context),
      mix_domain_(context_.threading_model().AcquireMixDomain("capturer")),
      state_(State::WaitingForVmo),
      reporter_(Reporter::Singleton().CreateCapturer(mix_domain_->name())),
      audio_clock_(context_.clock_factory()->CreateClientAdjustable(
          audio::clock::AdjustableCloneOfMonotonic())) {
  FX_DCHECK(mix_domain_);

  binding_.set_error_handler([this](zx_status_t status) { BeginShutdown(); });
  source_links_.reserve(16u);

  if (format) {
    UpdateFormat(*format);
  }

  zx_status_t status =
      ready_packets_wakeup_.Activate(context_.threading_model().FidlDomain().dispatcher(),
                                     [this](WakeupEvent* event) -> zx_status_t {
                                       FinishBuffersThunk();
                                       return ZX_OK;
                                     });
  FX_DCHECK(status == ZX_OK) << "Failed to activate FinishBuffers wakeup signal";
}

BaseCapturer::~BaseCapturer() { TRACE_DURATION("audio.debug", "BaseCapturer::~BaseCapturer"); }

void BaseCapturer::OnLinkAdded() { RecomputePresentationDelay(); }

void BaseCapturer::UpdateState(State new_state) {
  if (new_state == State::WaitingForRequest) {
    // Transitioning from initializing -> Sync or Async -> Sync: we need a new packet queue.
    set_packet_queue(CapturePacketQueue::CreateDynamicallyAllocated(payload_buf_, format_.value()));
  }
  if (new_state == State::Shutdown) {
    // This can be null if we shutdown before initialization completes.
    if (auto pq = packet_queue(); pq) {
      pq->Shutdown();
    }
  }
  State old_state = state_.exchange(new_state);
  if (old_state != new_state) {
    OnStateChanged(old_state, new_state);
  }
}

fpromise::promise<> BaseCapturer::Cleanup() {
  TRACE_DURATION("audio.debug", "BaseCapturer::Cleanup");

  // We need to stop all the async operations happening on the mix dispatcher. These components can
  // only be touched on that thread, so post a task there to run that cleanup.
  fpromise::bridge<> bridge;
  auto nonce = TRACE_NONCE();
  TRACE_FLOW_BEGIN("audio.debug", "BaseCapturer.capture_cleanup", nonce);
  async::PostTask(
      mix_domain_->dispatcher(),
      [self = shared_from_this(), completer = std::move(bridge.completer), nonce]() mutable {
        TRACE_DURATION("audio.debug", "BaseCapturer.cleanup_thunk");
        TRACE_FLOW_END("audio.debug", "BaseCapturer.capture_cleanup", nonce);
        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, self->mix_domain_);
        self->CleanupFromMixThread();
        completer.complete_ok();
      });

  // After CleanupFromMixThread is done, no more work will happen on the mix dispatch thread. We
  // need to now ensure our ready_packets signal is De-asserted.
  return bridge.consumer.promise().then(
      [this](fpromise::result<>&) { ready_packets_wakeup_.Deactivate(); });
}

void BaseCapturer::CleanupFromMixThread() {
  TRACE_DURATION("audio", "BaseCapturer::CleanupFromMixThread");

  mix_wakeup_.Deactivate();
  mix_timer_.Cancel();
  UpdateState(State::Shutdown);
}

void BaseCapturer::BeginShutdown() {
  context_.threading_model().FidlDomain().ScheduleTask(Cleanup().then(
      [this](fpromise::result<>&) { context_.route_graph().RemoveCapturer(*this); }));
}

void BaseCapturer::OnStateChanged(State old_state, State new_state) {
  bool was_routable = StateIsRoutable(old_state);
  bool is_routable = StateIsRoutable(new_state);
  if (was_routable != is_routable) {
    SetRoutingProfile(is_routable);
  }

  bool is_started = new_state == State::SyncOperating || new_state == State::AsyncOperating;
  bool was_started = old_state == State::SyncOperating || old_state == State::AsyncOperating;
  if (is_started && !was_started) {
    ReportStart();
  }
  if (was_started && !is_started) {
    ReportStop();
  }
}

fpromise::result<std::pair<std::shared_ptr<Mixer>, ExecutionDomain*>, zx_status_t>
BaseCapturer::InitializeSourceLink(const AudioObject& source,
                                   std::shared_ptr<ReadableStream> source_stream) {
  TRACE_DURATION("audio", "BaseCapturer::InitializeSourceLink");

  if (!format_.has_value()) {
    BeginShutdown();
    return fpromise::error(ZX_ERR_BAD_STATE);
  }

  switch (state_.load()) {
    // We are operational. Go ahead and add the input to our mix stage.
    case State::WaitingForRequest:
    case State::SyncOperating:
    case State::AsyncOperating:
    case State::AsyncStopping:
    case State::AsyncStoppingCallbackPending:
    case State::WaitingForVmo:
      // In capture, source clocks originate from devices (inputs if live, outputs if loopback).
      // For now, "loop in" (direct client-to-client) routing is unsupported.
      // For now, device clocks should not be adjustable.
      FX_CHECK(!source_stream->reference_clock()->adjustable());
      return fpromise::ok(
          std::make_pair(mix_stage_->AddInput(std::move(source_stream)), &mix_domain()));

    // If we are shut down, then I'm not sure why new links are being added, but
    // just go ahead and reject this one. We will be going away shortly.
    case State::Shutdown:
      return fpromise::error(ZX_ERR_BAD_STATE);
  }
}

void BaseCapturer::CleanupSourceLink(const AudioObject& source,
                                     std::shared_ptr<ReadableStream> source_stream) {
  mix_stage_->RemoveInput(*source_stream);
}

void BaseCapturer::GetStreamType(GetStreamTypeCallback cbk) {
  TRACE_DURATION("audio", "BaseCapturer::GetStreamType");
  fuchsia::media::StreamType ret;
  ret.encoding = fuchsia::media::AUDIO_ENCODING_LPCM;
  ret.medium_specific.set_audio(format_.value_or(kInitialFormat).stream_type());
  cbk(std::move(ret));
}

void BaseCapturer::AddPayloadBuffer(uint32_t id, zx::vmo payload_buf_vmo) {
  TRACE_DURATION("audio", "BaseCapturer::AddPayloadBuffer");
  if (!format_.has_value()) {
    FX_LOGS(WARNING) << "StreamType must be set before payload buffer is added.";
    BeginShutdown();
    return;
  }

  if (id != 0) {
    FX_LOGS(WARNING) << "Only buffer ID 0 is currently supported.";
    BeginShutdown();
    return;
  }

  FX_DCHECK(payload_buf_vmo.is_valid());

  // If something goes wrong, hang up the phone and shutdown.
  auto cleanup = fit::defer([this]() { BeginShutdown(); });
  zx_status_t res;

  State state = state_.load();
  if (state != State::WaitingForVmo) {
    FX_DCHECK(payload_buf_.start() != nullptr);
    FX_DCHECK(payload_buf_.size() != 0);
    FX_LOGS(ERROR) << "Bad state while assigning payload buffer "
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  FX_DCHECK(payload_buf_.start() == nullptr);
  FX_DCHECK(payload_buf_.size() == 0);

  size_t payload_buf_size;
  res = payload_buf_vmo.get_size(&payload_buf_size);
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Failed to fetch payload buffer VMO size";
    return;
  }

  constexpr uint64_t max_uint32 = std::numeric_limits<uint32_t>::max();
  if ((payload_buf_size < format_->bytes_per_frame()) ||
      (payload_buf_size > (max_uint32 * format_->bytes_per_frame()))) {
    FX_LOGS(ERROR) << "Bad payload buffer VMO size (size = " << payload_buf_.size()
                   << ", bytes per frame = " << format_->bytes_per_frame() << ")";
    return;
  }

  reporter_->AddPayloadBuffer(id, payload_buf_size);

  auto payload_buf_frames = static_cast<uint32_t>(payload_buf_size / format_->bytes_per_frame());
  FX_LOGS(DEBUG) << "payload buf -- size:" << payload_buf_size << ", frames:" << payload_buf_frames
                 << ", bytes/frame:" << format_->bytes_per_frame();

  // Map the VMO into our process.
  res = payload_buf_.Map(payload_buf_vmo, /*offset=*/0, payload_buf_size,
                         /*map_flags=*/ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Failed to map payload buffer VMO";
    return;
  }

  // Activate the dispatcher primitives we will use to drive the mixing process. Note we must call
  // Activate on the WakeupEvent from the mix domain, but Signal can be called anytime, even before
  // this Activate occurs.
  async::PostTask(mix_domain_->dispatcher(), [self = shared_from_this()] {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, self->mix_domain_);
    zx_status_t status = self->mix_wakeup_.Activate(
        self->mix_domain_->dispatcher(), [self](WakeupEvent* event) -> zx_status_t {
          OBTAIN_EXECUTION_DOMAIN_TOKEN(token, self->mix_domain_);
          FX_DCHECK(event == &self->mix_wakeup_);
          return self->Process();
        });

    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed activate mix WakeupEvent";
      self->ShutdownFromMixDomain();
      return;
    }
  });

  // Next, select our output producer.
  output_producer_ = OutputProducer::Select(format_->stream_type());
  if (output_producer_ == nullptr) {
    FX_LOGS(ERROR) << "Failed to select output producer";
    return;
  }

  // Mark ourselves as routable now that we're fully configured. Although we might still fail to
  // create links to audio sources, we have successfully configured this capturer's mode, so we are
  // now in the WaitingForRequest state.
  UpdateState(State::WaitingForRequest);
  cleanup.cancel();
}

void BaseCapturer::RemovePayloadBuffer(uint32_t id) {
  TRACE_DURATION("audio", "BaseCapturer::RemovePayloadBuffer");
  FX_LOGS(WARNING) << "RemovePayloadBuffer is not currently supported.";
  BeginShutdown();
}

bool BaseCapturer::IsOperating() {
  State state = state_.load();
  switch (state) {
    case State::SyncOperating:
    case State::AsyncOperating:
    case State::AsyncStopping:
    case State::AsyncStoppingCallbackPending:
      return true;
    default:
      return false;
  }
}

void BaseCapturer::CaptureAt(uint32_t payload_buffer_id, uint32_t offset_frames,
                             uint32_t num_frames, CaptureAtCallback cbk) {
  TRACE_DURATION("audio", "BaseCapturer::CaptureAt");
  if (payload_buffer_id != 0) {
    FX_LOGS(WARNING) << "payload_buffer_id must be 0 for now.";
    return;
  }

  // If something goes wrong, hang up the phone and shutdown.
  auto cleanup = fit::defer([this]() { BeginShutdown(); });

  // It is illegal to call CaptureAt unless we are currently operating in
  // synchronous mode.
  State state = state_.load();
  if (state != State::WaitingForRequest && state != State::SyncOperating) {
    FX_LOGS(WARNING) << "CaptureAt called while in wrong state "
                     << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  // Place the capture operation on the pending list.
  auto pq = packet_queue();
  auto was_empty = (pq->PendingSize() == 0);
  auto result = pq->PushPending(offset_frames, num_frames, std::move(cbk));
  if (!result.is_ok()) {
    FX_LOGS(WARNING) << "CaptureAt failed to create a new packet: " << result.error();
    return;
  }

  // If the pending list was empty, we need to poke the mixer.
  if (was_empty) {
    UpdateState(State::SyncOperating);
    mix_wakeup_.Signal();
  }

  // Things went well. Cancel the cleanup timer and we are done.
  cleanup.cancel();
}

void BaseCapturer::ReleasePacket(fuchsia::media::StreamPacket packet) {
  TRACE_DURATION("audio", "BaseCapturer::ReleasePacket");
  State state = state_.load();
  if (state != State::AsyncOperating) {
    FX_LOGS(WARNING) << "ReleasePacket called while not operating in async mode "
                     << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }
  packet_queue()->Recycle(packet);
}

void BaseCapturer::DiscardAllPacketsNoReply() {
  TRACE_DURATION("audio", "BaseCapturer::DiscardAllPacketsNoReply");
  DiscardAllPackets(nullptr);
}

void BaseCapturer::DiscardAllPackets(DiscardAllPacketsCallback cbk) {
  TRACE_DURATION("audio", "BaseCapturer::DiscardAllPackets");
  // It is illegal to call DiscardAllPackets unless we are currently operating in
  // synchronous mode.
  State state = state_.load();
  if (state != State::WaitingForRequest && state != State::SyncOperating) {
    FX_LOGS(WARNING) << "DiscardAllPackets called while not in wrong state "
                     << "(state = " << static_cast<uint32_t>(state) << ")";
    BeginShutdown();
    return;
  }

  // Note: the capture thread may currently be mixing frames for the buffer at the head of the
  // pending queue, when the queue is cleared. The fact that these frames were mixed will not be
  // reported to the client; however, the frames will be written to the shared payload buffer.
  auto pq = packet_queue();
  pq->DiscardPendingPackets();
  if (pq->ReadySize() > 0) {
    FinishBuffers();
    binding_.events().OnEndOfStream();
  }

  if (state != State::WaitingForRequest) {
    UpdateState(State::WaitingForRequest);
  }

  if (cbk != nullptr && binding_.is_bound()) {
    cbk();
  }
}

void BaseCapturer::StartAsyncCapture(uint32_t frames_per_packet) {
  TRACE_DURATION("audio", "BaseCapturer::StartAsyncCapture");
  auto cleanup = fit::defer([this]() { BeginShutdown(); });

  // To enter Async mode, we must be in Synchronous mode and not have packets in flight.
  State state = state_.load();
  if (state != State::WaitingForRequest) {
    FX_LOGS(WARNING) << "Bad state while attempting to enter async capture mode "
                     << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  if (!packet_queue()->empty()) {
    FX_LOGS(WARNING) << "Attempted to enter async capture mode with packets still in flight.";
    return;
  }

  // Allocate an asynchronous queue.
  auto result =
      CapturePacketQueue::CreatePreallocated(payload_buf_, format_.value(), frames_per_packet);
  if (!result.is_ok()) {
    FX_LOGS(WARNING) << "StartAsyncCapture failed: " << result.error();
    return;
  }

  // Transition to the AsyncOperating state.
  set_packet_queue(result.take_value());
  UpdateState(State::AsyncOperating);

  // Kick the work thread to get the ball rolling.
  mix_wakeup_.Signal();
  cleanup.cancel();
}

void BaseCapturer::StopAsyncCaptureNoReply() {
  TRACE_DURATION("audio", "BaseCapturer::StopAsyncCaptureNoReply");
  StopAsyncCapture(nullptr);
}

void BaseCapturer::StopAsyncCapture(StopAsyncCaptureCallback cbk) {
  TRACE_DURATION("audio", "BaseCapturer::StopAsyncCapture");
  // To leave async mode, we must be (1) in Async mode or (2) already in Sync mode (in which case,
  // there is really nothing to do but signal the callback if one was provided).
  State state = state_.load();
  if (state == State::WaitingForRequest || state == State::SyncOperating) {
    if (cbk != nullptr) {
      cbk();
    }
    return;
  }

  if (state != State::AsyncOperating) {
    FX_LOGS(WARNING) << "Bad state while attempting to stop async capture mode "
                     << "(state = " << static_cast<uint32_t>(state) << ")";
    BeginShutdown();
    return;
  }

  // We're done with this packet queue. We're transititioning to WaitingForRequest.
  // Later, if we transition back to AsyncOperating, we'll create a brand new packet queue.
  packet_queue()->Shutdown();

  // Stash our callback, transition to AsyncStopping, then poke the work thread to shut down.
  FX_DCHECK(pending_async_stop_cbk_ == nullptr);
  pending_async_stop_cbk_ = std::move(cbk);
  UpdateState(State::AsyncStopping);
  mix_wakeup_.Signal();
}

void BaseCapturer::ReportStart() { reporter_->StartSession(zx::clock::get_monotonic()); }

void BaseCapturer::ReportStop() { reporter_->StopSession(zx::clock::get_monotonic()); }

// Note that each source is returning presentation delay based on ITS OWN clock, so comparing them
// (with std::max) is not strictly accurate. Also, we THEN store the worst-case presentation delay
// and use it in our position calculations, which are based on OUR clock.
//
// Accurately incorporating clock differences into this delay would require us to recompute it for
// every mix, since device clocks and capture clocks can be rate-adjusted at any time. Continuous
// recalculation seems excessive, as the vast majority of clocks will differ by +/-10 ppm.
//
// That said, in the worst-case a clock can diverge from MONOTONIC by +/- 1000 ppm, so the maximum
// rate difference between two clocks is 2000 ppm or 0.2%. Fortunately, kPresentationDelayPadding
// includes a 2x safety factor (1.5 ms is considered sufficient; it is set to 3 ms). This extra
// padding is greater than our 0.2% of uncertainty, for any capture presentation delays less than
// 750 ms. That said, kPresentationDelayPadding assumes that the longest capture mix is 1.5 ms -- an
// assumption that should be verified/updated.
// TODO(fxbug.dev/91258): pad this further if needed, based on worst-case capture mix measurements.
// Or reconsider continuously recalculating this delay.
void BaseCapturer::RecomputePresentationDelay() {
  TRACE_DURATION("audio", "BaseCapturer::RecomputePresentationDelay");

  zx::duration cur_max{0};
  context_.link_matrix().ForEachSourceLink(*this, [&cur_max](LinkMatrix::LinkHandle link) {
    if (link.object->is_input()) {
      const auto& device = static_cast<const AudioDevice&>(*link.object);
      cur_max = std::max(cur_max, device.presentation_delay());
    }
  });

  cur_max += kPresentationDelayPadding;
  if (presentation_delay_ != cur_max) {
    FX_LOGS(TRACE) << "Changing presentation_delay_ (ns) from " << presentation_delay_.get()
                   << " to " << cur_max.get();

    reporter_->SetMinFenceTime(cur_max);
    presentation_delay_ = cur_max;
  }
}

zx_status_t BaseCapturer::Process() {
  TRACE_DURATION("audio", "BaseCapturer::Process");
  while (true) {
    // Start by figure out what state we are currently in for this cycle.
    switch (state_.load()) {
      // If we are still waiting for a VMO, we should not be operating right now.
      case State::WaitingForVmo:
        FX_DCHECK(false);
        ShutdownFromMixDomain();
        return ZX_ERR_INTERNAL;

      // Spurious wakeups: there are not pending packets to fill.
      case State::WaitingForRequest:
      case State::AsyncStoppingCallbackPending:
        return ZX_OK;

      // If we were operating in async mode, but we have been asked to stop, do so now.
      case State::AsyncStopping:
        DoStopAsyncCapture();
        return ZX_OK;

      case State::SyncOperating:
        break;

      case State::AsyncOperating:
        break;

      case State::Shutdown:
        // This should be impossible. If the main message loop thread shut us down, then it should
        // have shut down our mix timer before  setting the state_ variable to Shutdown.
        FX_CHECK(false);
        return ZX_ERR_INTERNAL;
    }

    // Hold onto this reference for the duration of this mix operation in case
    // the queue is swapped out from under us.
    auto pq = packet_queue();
    FX_CHECK(pq);

    // Look at the head of the queue, determine our payload buffer position, and get to work.
    auto mix_state = pq->NextMixerJob();

    // If there was nothing in our pending capture buffer queue, then one of two things is true:
    //
    // 1) We are SyncOperating and our user is not supplying packets fast enough.
    // 2) We are AsyncOperating and our user is not releasing packets fast enough.
    //
    // Either way, this is an overflow. Invalidate the frames_to_ref_clock transformation and make
    // sure we don't have a wakeup timer pending.
    if (!mix_state) {
      discontinuity_ = true;
      mix_timer_.Cancel();

      if (state_.load() == State::SyncOperating) {
        return ZX_OK;
      }

      // Wait until we have another packet or have shut down.
      // This waits for the caller to ACK a packet, so it might block indefinitely.
      auto overflow_start = zx::clock::get_monotonic();
      pq->WaitForPendingPacket();
      if (state_.load() == State::Shutdown) {
        return ZX_OK;
      }

      auto overflow_end = zx::clock::get_monotonic();
      ReportOverflow(overflow_start, overflow_end);

      // Have another packet: continue capturing.
      continue;
    }

    // Limit our job size to our max job size.
    if (mix_state->frames > max_frames_per_capture_) {
      mix_state->frames = max_frames_per_capture_;
    }

    // Establish the frame pointer.
    // We continue at the current frame pointer, unless there was a discontinuity,
    // at which point we need to recompute the frame pointer.
    auto dest_ref_now = reference_clock()->now();
    auto [dest_ref_pts_to_frac_frame, _] = ref_pts_to_fractional_frame_->get();
    FX_CHECK(dest_ref_pts_to_frac_frame.invertible());

    if (discontinuity_) {
      // On discontinuities, align the target frame with the current time.
      discontinuity_ = false;
      mix_state->flags |= fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY;
      frame_pointer_ = Fixed::FromRaw(dest_ref_pts_to_frac_frame.Apply(dest_ref_now.get())).Floor();
    }

    // If we woke too soon to perform the requested mix, sleep until we can read the last frame.
    //
    // Note that presentation_delay_ is inherently inaccurate by up to 2000 ppm (see comment where
    // kPresentationDelayPadding is defined), but we have padded this duration in order to (among
    // other things) accommodate worst-case difference between source/capturer ref clocks.
    auto dest_ref_safe_time = dest_ref_now - presentation_delay_;
    int64_t dest_safe_frame =
        Fixed::FromRaw(dest_ref_pts_to_frac_frame.Apply(dest_ref_safe_time.get())).Floor();
    int64_t dest_last_frame = frame_pointer_ + mix_state->frames;
    if (dest_last_frame > dest_safe_frame) {
      auto dest_ref_last_frame_time =
          zx::time(dest_ref_pts_to_frac_frame.Inverse().Apply(Fixed(dest_last_frame).raw_value()));

      auto dest_ref_wakeup_time = dest_ref_last_frame_time + presentation_delay_;
      auto mono_wakeup_time =
          reference_clock()->MonotonicTimeFromReferenceTime(dest_ref_wakeup_time);

      zx_status_t status = mix_timer_.PostForTime(mix_domain_->dispatcher(), mono_wakeup_time);
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "Failed to schedule capturer mix";
        ShutdownFromMixDomain();
        return ZX_ERR_INTERNAL;
      }

      // We can't complete this mix yet, so the mix should not be "done".
      mix_state->frames = 0;
      auto job_status = pq->FinishMixerJob(*mix_state);
      FX_DCHECK(job_status != CapturePacketQueue::PacketMixStatus::Done);
      return ZX_OK;
    }

    // Assign a timestamp if one has not already been assigned.
    if (mix_state->capture_timestamp == fuchsia::media::NO_TIMESTAMP) {
      mix_state->capture_timestamp =
          dest_ref_pts_to_frac_frame.Inverse().Apply(Fixed(frame_pointer_).raw_value());
    }

    // Mix the requested number of frames.
    ReadableStream::ReadLockContext ctx;
    auto buf = mix_stage_->ReadLock(ctx, Fixed(frame_pointer_), mix_state->frames);
    if (buf) {
      FX_DCHECK(buf->start().Floor() == frame_pointer_);
      FX_DCHECK(buf->length() > 0);
      FX_DCHECK(static_cast<size_t>(buf->length()) == mix_state->frames);
      output_producer_->ProduceOutput(reinterpret_cast<float*>(buf->payload()), mix_state->target,
                                      mix_state->frames);
    } else {
      // If we didn't get a buffer from the mix stage then we only have silence.
      output_producer_->FillWithSilence(mix_state->target, mix_state->frames);
    }

    // Complete this mix job.
    switch (pq->FinishMixerJob(*mix_state)) {
      case CapturePacketQueue::PacketMixStatus::Done:
        // If we filled the entire packet, wake the FIDL thread.
        ready_packets_wakeup_.Signal();
        if (auto s = pq->ReadySize(); s > 0 && s % 20 == 0) {
          FX_LOGS_FIRST_N(WARNING, 100)
              << "Process producing a lot of packets " << s << " @ frame " << frame_pointer_;
        }
        break;

      case CapturePacketQueue::PacketMixStatus::Partial:
        // Did not fill the entire packet yet.
        break;

      case CapturePacketQueue::PacketMixStatus::Discarded:
        // It looks like we were flushed while we were mixing: the next mix is not continuous.
        discontinuity_ = true;
        break;
    }

    // Update the total number of frames we have mixed so far.
    frame_pointer_ += mix_state->frames;
  }  // while (true)
}

void BaseCapturer::ReportOverflow(zx::time start_time, zx::time end_time) {
  TRACE_INSTANT("audio", "BaseCapturer::OVERFLOW", TRACE_SCOPE_THREAD);
  TRACE_ALERT("audio", "audiooverflow");

  overflow_count_++;
  if constexpr (kLogCaptureOverflow) {
    auto duration_ms = static_cast<double>((end_time - start_time).to_nsecs()) / ZX_MSEC(1);
    if ((overflow_count_ - 1) % kCaptureOverflowWarningInterval == 0) {
      FX_LOGS(WARNING) << "CAPTURE OVERERFLOW #" << overflow_count_ << " lasted "
                       << std::setprecision(4) << duration_ms << " ms";
    } else if ((overflow_count_ - 1) % kCaptureOverflowInfoInterval == 0) {
      FX_LOGS(INFO) << "CAPTURE OVERERFLOW #" << overflow_count_ << " lasted "
                    << std::setprecision(4) << duration_ms << " ms";
    } else {
      FX_LOGS(TRACE) << "CAPTURE OVERERFLOW #" << overflow_count_ << " lasted "
                     << std::setprecision(4) << duration_ms << " ms";
    }
  }

  reporter().Overflow(start_time, end_time);
}

void BaseCapturer::DoStopAsyncCapture() {
  TRACE_DURATION("audio", "BaseCapturer::DoStopAsyncCapture");
  // If this is being called, we had better be in the async stopping state.
  FX_DCHECK(state_.load() == State::AsyncStopping);

  // Discard all pending packets. We won't need to worry about recycling these,
  // because we're transitioning back to SyncOperating, at which time we'll create
  // an entirely new CapturePacketQueue.
  packet_queue()->DiscardPendingPackets();
  discontinuity_ = true;

  // If we had a timer set, make sure that it is canceled. There is no point in
  // having it armed right now as we are in the process of stopping.
  mix_timer_.Cancel();

  // Transition to the AsyncStoppingCallbackPending state, and signal the
  // service thread so it can complete the stop operation.
  UpdateState(State::AsyncStoppingCallbackPending);
  async::PostTask(context_.threading_model().FidlDomain().dispatcher(),
                  [self = shared_from_this()]() { self->FinishAsyncStopThunk(); });
}

void BaseCapturer::ShutdownFromMixDomain() {
  TRACE_DURATION("audio", "BaseCapturer::ShutdownFromMixDomain");
  async::PostTask(context_.threading_model().FidlDomain().dispatcher(),
                  [self = shared_from_this()]() { self->BeginShutdown(); });
}

void BaseCapturer::FinishAsyncStopThunk() {
  TRACE_DURATION("audio", "BaseCapturer::FinishAsyncStopThunk");
  // Do nothing if we were shutdown between the time that this message was
  // posted to the main message loop and the time that we were dispatched.
  if (state_.load() == State::Shutdown) {
    return;
  }

  // Start by sending back all of our completed buffers. Finish up by sending
  // an OnEndOfStream event.
  FinishBuffers();
  binding_.events().OnEndOfStream();

  // If we have a valid callback to make, call it now.
  if (pending_async_stop_cbk_ != nullptr) {
    pending_async_stop_cbk_();
    pending_async_stop_cbk_ = nullptr;
  }

  UpdateState(State::WaitingForRequest);
}

void BaseCapturer::FinishBuffersThunk() {
  TRACE_DURATION("audio", "BaseCapturer::FinishBuffersThunk");
  // Do nothing if we were shutdown between the time that this message was
  // posted to the main message loop and the time that we were dispatched.
  if (state_.load() == State::Shutdown) {
    return;
  }
  FinishBuffers();
}

void BaseCapturer::FinishBuffers() {
  TRACE_DURATION("audio", "BaseCapturer::FinishBuffers");

  auto pq = packet_queue();
  if (pq->ReadySize() > 50) {
    FX_LOGS_FIRST_N(WARNING, 100) << "Finishing large batch of capture buffers: "
                                  << pq->ReadySize();
  }

  bool warned_slow = false;
  while (pq->ReadySize() > 0) {
    auto p = pq->PopReady();

    if (!warned_slow) {
      if (auto t = p->time_since_ready(); t > zx::msec(500)) {
        FX_LOGS(WARNING) << "FinishBuffers took " << t.to_msecs() << "ms to schedule";
        warned_slow = true;
      }
    }

    // If there is no callback tied to this buffer (meaning that it was generated while operating in
    // async mode), and it is not filled at all, just skip it.
    if (p->callback() == nullptr && p->stream_packet().payload_size == 0) {
      continue;
    }

    auto& pkt = p->stream_packet();
    reporter_->SendPacket(pkt);

    if (p->callback() != nullptr) {
      FX_LOGS(TRACE) << "Sync -mode -- payload size:" << pkt.payload_size
                     << " bytes, offset:" << pkt.payload_offset << " bytes, flags:" << pkt.flags
                     << ", pts:" << pkt.pts;

      p->callback()(pkt);
    } else {
      FX_LOGS(TRACE) << "Async-mode -- payload size:" << pkt.payload_size
                     << " bytes, offset:" << pkt.payload_offset << " bytes, flags:" << pkt.flags
                     << ", pts:" << pkt.pts;

      binding_.events().OnPacketProduced(pkt);
    }
  }

  if (state_.load() == State::SyncOperating && pq->PendingSize() == 0) {
    UpdateState(State::WaitingForRequest);
  }
}

void BaseCapturer::UpdateFormat(Format format) {
  TRACE_DURATION("audio", "BaseCapturer::UpdateFormat");
  // Record our new format.
  FX_DCHECK(state_.load() == State::WaitingForVmo);
  format_ = {format};

  reporter().SetFormat(format);

  auto dest_ref_now = reference_clock()->now();
  ref_pts_to_fractional_frame_->Update(TimelineFunction(
      0, dest_ref_now.get(), Fixed(format_->frames_per_second()).raw_value(), zx::sec(1).get()));

  // Pre-compute the ratio between frames and clock mono ticks. Also figure out
  // the maximum number of frames we are allowed to mix and capture at a time.
  //
  // Some sources (like AudioOutputs or TapStages) have a limited amount of time which they
  // are able to hold onto data after presentation. We need to wait until after
  // presentation time to capture these frames, but if we batch up too much
  // work, then the AudioOutput may have overwritten the data before we decide
  // to get around to capturing it. By limiting our maximum number of frames to
  // capture to be less than this amount of time, we prevent this issue.
  int64_t tmp;
  tmp = dest_frames_to_ref_clock_rate().Inverse().Scale(kMaxTimePerCapture);
  max_frames_per_capture_ = static_cast<uint32_t>(tmp);

  FX_DCHECK(tmp <= std::numeric_limits<uint32_t>::max());
  FX_DCHECK(max_frames_per_capture_ > 0);

  // MixStage always emits floats.
  auto mix_stage_format =
      Format::Create({
                         .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                         .channels = static_cast<uint32_t>(format_->channels()),
                         .frames_per_second = static_cast<uint32_t>(format_->frames_per_second()),
                     })
          .take_value();

  // Allocate our MixStage for mixing.
  //
  // TODO(fxbug.dev/39886): Limit this to something smaller than one second of frames.
  uint32_t max_mix_frames = format_->frames_per_second();
  mix_stage_ = std::make_shared<MixStage>(mix_stage_format, max_mix_frames,
                                          ref_pts_to_fractional_frame_, reference_clock());
}

// Regardless of the source of the reference clock, we can duplicate and return it here.
void BaseCapturer::GetReferenceClock(GetReferenceClockCallback callback) {
  TRACE_DURATION("audio", "BaseCapturer::GetReferenceClock");

  auto cleanup = fit::defer([this]() { BeginShutdown(); });

  // Regardless of whether clock_ is writable, this strips off the WRITE right.
  auto clock_result = audio_clock_->DuplicateZxClockReadOnly();
  if (!clock_result) {
    FX_LOGS(ERROR) << "DuplicateZxClockReadOnly failed, will not return reference clock!";
    return;
  }

  callback(std::move(*clock_result));
  cleanup.cancel();
}

}  // namespace media::audio
