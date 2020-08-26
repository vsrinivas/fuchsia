// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/base_capturer.h"

#include <lib/fit/bridge.h>
#include <lib/fit/defer.h>
#include <lib/media/audio/cpp/types.h>
#include <lib/zx/clock.h>

#include <memory>

#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/audio_core_impl.h"
#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {
namespace {

// To what extent should client-side overflows be logged? (A "client-side overflow" refers to when
// part of a data section is discarded because its start timestamp had passed.) For each Capturer,
// we will log the first overflow. For subsequent occurrences, depending on audio_core's logging
// level, we throttle how frequently these are displayed. If log_level is set to TRACE or SPEW, all
// client-side overflows are logged. If set to INFO or higher, we log less often.
static constexpr bool kLogCaptureOverflow = true;
static constexpr uint16_t kCaptureOverflowInfoInterval = 10;
static constexpr uint16_t kCaptureOverflowErrorInterval = 100;

// Currently, the time we spend mixing must also be taken into account when reasoning about the
// capture fence duration. Today (before any attempt at optimization), a particularly heavy mix
// pass may take longer than 1.5 msec on a DEBUG build(!) on relevant hardware. The constant below
// accounts for this, with additional padding for safety.
const zx::duration kFenceTimePadding = zx::msec(3);

constexpr int64_t kMaxTimePerCapture = ZX_MSEC(50);

const Format kInitialFormat =
    Format::Create({.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                    .channels = 1,
                    .frames_per_second = 8000})
        .take_value();

}  // namespace

bool BaseCapturer::must_release_packets_ = false;

BaseCapturer::BaseCapturer(
    std::optional<Format> format,
    fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request, Context* context)
    : AudioObject(Type::AudioCapturer),
      binding_(this, std::move(audio_capturer_request)),
      context_(*context),
      mix_domain_(context_.threading_model().AcquireMixDomain()),
      state_(State::WaitingForVmo),
      min_fence_time_(zx::nsec(0)),
      // Ideally, initialize this to the native configuration of our initially-bound source.
      format_(kInitialFormat),
      reporter_(Reporter::Singleton().CreateCapturer()) {
  FX_DCHECK(mix_domain_);

  // For now, optimal clock is set as a clone of MONOTONIC. Ultimately this will be the clock of
  // the device where the capturer is initially routed.
  SetOptimalReferenceClock();

  binding_.set_error_handler([this](zx_status_t status) { BeginShutdown(); });
  source_links_.reserve(16u);

  if (format) {
    UpdateFormat(*format);
  } else {
    reporter().SetFormat(format_);
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

void BaseCapturer::OnLinkAdded() { RecomputeMinFenceTime(); }

void BaseCapturer::UpdateState(State new_state) {
  if (new_state == State::OperatingSync) {
    set_packet_queue(CapturePacketQueue::CreateDynamicallyAllocated(payload_buf_, format_));
  }
  if (new_state == State::Shutdown) {
    // This can be null if we shutdown before initialization completes.
    if (auto pq = packet_queue(); pq) {
      pq->Shutdown();
    }
  }
  State old_state = state_.exchange(new_state);
  OnStateChanged(old_state, new_state);
}

fit::promise<> BaseCapturer::Cleanup() {
  TRACE_DURATION("audio.debug", "BaseCapturer::Cleanup");

  // We need to stop all the async operations happening on the mix dispatcher. These components can
  // only be touched on that thread, so post a task there to run that cleanup.
  fit::bridge<> bridge;
  auto nonce = TRACE_NONCE();
  TRACE_FLOW_BEGIN("audio.debug", "BaseCapturer.capture_cleanup", nonce);
  async::PostTask(mix_domain_->dispatcher(),
                  [this, completer = std::move(bridge.completer), nonce]() mutable {
                    TRACE_DURATION("audio.debug", "BaseCapturer.cleanup_thunk");
                    TRACE_FLOW_END("audio.debug", "BaseCapturer.capture_cleanup", nonce);
                    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, mix_domain_);
                    CleanupFromMixThread();
                    completer.complete_ok();
                  });

  // After CleanupFromMixThread is done, no more work will happen on the mix dispatch thread. We
  // need to now ensure our ready_packets signal is De-asserted.
  return bridge.consumer.promise().then(
      [this](fit::result<>&) { ready_packets_wakeup_.Deactivate(); });
}

void BaseCapturer::CleanupFromMixThread() {
  TRACE_DURATION("audio", "BaseCapturer::CleanupFromMixThread");

  mix_wakeup_.Deactivate();
  mix_timer_.Cancel();
  mix_domain_ = nullptr;
  UpdateState(State::Shutdown);
}

void BaseCapturer::BeginShutdown() {
  context_.threading_model().FidlDomain().ScheduleTask(Cleanup().then([this](fit::result<>&) {
    ReportStop();
    context_.route_graph().RemoveCapturer(*this);
  }));
}

void BaseCapturer::OnStateChanged(State old_state, State new_state) {
  bool was_routable = StateIsRoutable(old_state);
  bool is_routable = StateIsRoutable(new_state);
  if (was_routable != is_routable) {
    SetRoutingProfile(is_routable);
  }
}

fit::result<std::pair<std::shared_ptr<Mixer>, ExecutionDomain*>, zx_status_t>
BaseCapturer::InitializeSourceLink(const AudioObject& source,
                                   std::shared_ptr<ReadableStream> stream) {
  TRACE_DURATION("audio", "BaseCapturer::InitializeSourceLink");

  switch (state_.load()) {
    // We are operational. Go ahead and add the input to our mix stage.
    case State::OperatingSync:
    case State::OperatingAsync:
    case State::AsyncStopping:
    case State::AsyncStoppingCallbackPending:
    case State::WaitingForVmo:
      return fit::ok(std::make_pair(mix_stage_->AddInput(std::move(stream)), &mix_domain()));

    // If we are shut down, then I'm not sure why new links are being added, but
    // just go ahead and reject this one. We will be going away shortly.
    case State::Shutdown:
      return fit::error(ZX_ERR_BAD_STATE);
  }
}

void BaseCapturer::CleanupSourceLink(const AudioObject& source,
                                     std::shared_ptr<ReadableStream> stream) {
  mix_stage_->RemoveInput(*stream);
}

void BaseCapturer::GetStreamType(GetStreamTypeCallback cbk) {
  TRACE_DURATION("audio", "BaseCapturer::GetStreamType");
  fuchsia::media::StreamType ret;
  ret.encoding = fuchsia::media::AUDIO_ENCODING_LPCM;
  ret.medium_specific.set_audio(format_.stream_type());
  cbk(std::move(ret));
}

void BaseCapturer::AddPayloadBuffer(uint32_t id, zx::vmo payload_buf_vmo) {
  TRACE_DURATION("audio", "BaseCapturer::AddPayloadBuffer");
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
  if ((payload_buf_size < format_.bytes_per_frame()) ||
      (payload_buf_size > (max_uint32 * format_.bytes_per_frame()))) {
    FX_LOGS(ERROR) << "Bad payload buffer VMO size (size = " << payload_buf_.size()
                   << ", bytes per frame = " << format_.bytes_per_frame() << ")";
    return;
  }

  reporter_->AddPayloadBuffer(id, payload_buf_size);

  auto payload_buf_frames = static_cast<uint32_t>(payload_buf_size / format_.bytes_per_frame());
  AUDIO_LOG_OBJ(DEBUG, this) << "payload buf -- size:" << payload_buf_size
                             << ", frames:" << payload_buf_frames
                             << ", bytes/frame:" << format_.bytes_per_frame();

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
  async::PostTask(mix_domain_->dispatcher(), [this] {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, mix_domain_);
    zx_status_t status =
        mix_wakeup_.Activate(mix_domain_->dispatcher(), [this](WakeupEvent* event) -> zx_status_t {
          OBTAIN_EXECUTION_DOMAIN_TOKEN(token, mix_domain_);
          FX_DCHECK(event == &mix_wakeup_);
          return Process();
        });

    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed activate mix WakeupEvent";
      ShutdownFromMixDomain();
      return;
    }
  });

  // Next, select our output producer.
  output_producer_ = OutputProducer::Select(format_.stream_type());
  if (output_producer_ == nullptr) {
    FX_LOGS(ERROR) << "Failed to select output producer";
    return;
  }

  // Mark ourselves as routable now that we're fully configured. Although we might still fail to
  // create links to audio sources, we have successfully configured this capturer's mode, so we are
  // now in the OperatingSync state.
  UpdateState(State::OperatingSync);
  cleanup.cancel();
}

void BaseCapturer::RemovePayloadBuffer(uint32_t id) {
  TRACE_DURATION("audio", "BaseCapturer::RemovePayloadBuffer");
  FX_LOGS(WARNING) << "RemovePayloadBuffer is not currently supported.";
  BeginShutdown();
}

// Are we actively capturing: either OperatingAsync, or OperatingSync with pending capture buffers
bool BaseCapturer::IsOperating() {
  State state = state_.load();
  switch (state) {
    // If OperatingAsync, we are actively generating capture packets
    case State::OperatingAsync:
    case State::AsyncStopping:
    case State::AsyncStoppingCallbackPending:
      return true;

    // If OperatingSync, then a pending packet means one or more CaptureAt() is pending.
    // Else, CaptureAt has never been called or has completed: no capture operation is active.
    case State::OperatingSync:
      return has_pending_packets();

    // Otherwise, the capturer is still being initialized or is being shutdown
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
  if (state != State::OperatingSync) {
    FX_LOGS(WARNING) << "CaptureAt called while not operating in sync mode "
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
    mix_wakeup_.Signal();
  }
  ReportStart();

  // Things went well. Cancel the cleanup timer and we are done.
  cleanup.cancel();
}

void BaseCapturer::ReleasePacket(fuchsia::media::StreamPacket packet) {
  TRACE_DURATION("audio", "BaseCapturer::ReleasePacket");
  State state = state_.load();
  if (state != State::OperatingAsync) {
    FX_LOGS(WARNING) << "CaptureAt called while not operating in async mode "
                     << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }
  // TODO(fxbug.dev/43507): Remove this flag.
  if (!must_release_packets_) {
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
  if (state != State::OperatingSync) {
    FX_LOGS(WARNING) << "DiscardAllPackets called while not operating in sync mode "
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

  ReportStop();

  if (cbk != nullptr && binding_.is_bound()) {
    cbk();
  }
}

void BaseCapturer::StartAsyncCapture(uint32_t frames_per_packet) {
  TRACE_DURATION("audio", "BaseCapturer::StartAsyncCapture");
  auto cleanup = fit::defer([this]() { BeginShutdown(); });

  // To enter Async mode, we must be in Synchronous mode and not have packets in flight.
  State state = state_.load();
  if (state != State::OperatingSync) {
    FX_LOGS(WARNING) << "Bad state while attempting to enter async capture mode "
                     << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  if (!packet_queue()->empty()) {
    FX_LOGS(WARNING) << "Attempted to enter async capture mode with packets still in flight.";
    return;
  }

  // Allocate an asynchronous queue.
  auto result = CapturePacketQueue::CreatePreallocated(payload_buf_, format_, frames_per_packet);
  if (!result.is_ok()) {
    FX_LOGS(WARNING) << "StartAsyncCapture failed: " << result.error();
    return;
  }

  // Transition to the OperatingAsync state.
  set_packet_queue(result.take_value());
  UpdateState(State::OperatingAsync);
  ReportStart();

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
  if (state == State::OperatingSync) {
    if (cbk != nullptr) {
      cbk();
    }
    return;
  }

  if (state != State::OperatingAsync) {
    FX_LOGS(WARNING) << "Bad state while attempting to stop async capture mode "
                     << "(state = " << static_cast<uint32_t>(state) << ")";
    BeginShutdown();
    return;
  }

  // Stash our callback, transition to AsyncStopping, then poke the work thread to shut down.
  FX_DCHECK(pending_async_stop_cbk_ == nullptr);
  pending_async_stop_cbk_ = std::move(cbk);
  ReportStop();
  UpdateState(State::AsyncStopping);
  mix_wakeup_.Signal();
}

void BaseCapturer::RecomputeMinFenceTime() {
  TRACE_DURATION("audio", "BaseCapturer::RecomputeMinFenceTime");

  zx::duration cur_min_fence_time{0};
  context_.link_matrix().ForEachSourceLink(
      *this, [&cur_min_fence_time](LinkMatrix::LinkHandle link) {
        if (link.object->is_input()) {
          const auto& device = static_cast<const AudioDevice&>(*link.object);
          auto fence_time = device.driver()->fifo_depth_duration();

          cur_min_fence_time = std::max(cur_min_fence_time, fence_time);
        }
      });

  if (min_fence_time_ != cur_min_fence_time) {
    FX_LOGS(TRACE) << "Changing min_fence_time_ (ns) from " << min_fence_time_.get() << " to "
                   << cur_min_fence_time.get();

    reporter_->SetMinFenceTime(cur_min_fence_time);
    min_fence_time_ = cur_min_fence_time;
  }
}

zx_status_t BaseCapturer::Process() {
  TRACE_DURATION("audio", "BaseCapturer::Process");
  while (true) {
    // Start by figure out what state we are currently in for this cycle.
    bool async_mode = false;
    switch (state_.load()) {
      // If we are still waiting for a VMO, we should not be operating right now.
      case State::WaitingForVmo:
        FX_DCHECK(false);
        ShutdownFromMixDomain();
        return ZX_ERR_INTERNAL;

      // If we are awakened while in the callback pending state, this is spurious wakeup: ignore it.
      case State::AsyncStoppingCallbackPending:
        return ZX_OK;

      // If we were operating in async mode, but we have been asked to stop, do so now.
      case State::AsyncStopping:
        DoStopAsyncCapture();
        return ZX_OK;

      case State::OperatingSync:
        async_mode = false;
        break;

      case State::OperatingAsync:
        async_mode = true;
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
    // 1) We are OperatingSync and our user is not supplying packets fast enough.
    // 2) We are OperatingAsync and our user is not releasing packets fast enough.
    //
    // Either way, this is an overflow. Invalidate the frames_to_ref_clock transformation and make
    // sure we don't have a wakeup timer pending.
    if (!mix_state) {
      ref_clock_to_fractional_dest_frames_->Update(TimelineFunction());
      frame_count_ = 0;
      mix_timer_.Cancel();

      if (state_.load() == State::OperatingSync) {
        ReportStop();
        return ZX_OK;
      }

      // Wait until we have another packet or have shut down.
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

    // Establish the transform from capture frames to clock monotonic, if we haven't already.
    //
    // Ideally, if there were only one capture source and our frame rates match, we would align our
    // start time exactly with a source sample boundary.
    auto ref_now = reference_clock().Read();

    if (!ref_clock_to_fractional_dest_frames_->get().first.invertible()) {
      // This packet is guaranteed to be discontinuous relative to the previous one (if any).
      mix_state->flags |= fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY;
      // Ideally a timeline function could alter offsets without also recalculating the scale
      // factor. Then we could re-establish this function without re-reducing the fps-to-nsec rate.
      // Since we supply a rate that is already reduced, this should go pretty quickly.
      ref_clock_to_fractional_dest_frames_->Update(
          TimelineFunction(Fixed(frame_count_).raw_value(), ref_now.get(),
                           fractional_dest_frames_to_ref_clock_rate().Inverse()));
    }

    // Assign a timestamp if one has not already been assigned.
    if (mix_state->capture_timestamp == fuchsia::media::NO_TIMESTAMP) {
      auto [ref_clock_to_fractional_dest_frames, _] = ref_clock_to_fractional_dest_frames_->get();
      FX_DCHECK(ref_clock_to_fractional_dest_frames.invertible());
      mix_state->capture_timestamp =
          ref_clock_to_fractional_dest_frames.Inverse().Apply(Fixed(frame_count_).raw_value());
    }

    // Limit our job size to our max job size.
    if (mix_state->frames > max_frames_per_capture_) {
      mix_state->frames = max_frames_per_capture_;
    }

    // Figure out when we can finish the job. If in the future, wait until then.
    zx::time last_frame_ref_time =
        zx::time(ref_clock_to_fractional_dest_frames_->get().first.Inverse().Apply(
            Fixed(frame_count_ + mix_state->frames).raw_value()));
    if (last_frame_ref_time.get() == TimelineRate::kOverflow) {
      FX_LOGS(ERROR) << "Fatal timeline overflow in capture mixer, shutting down capture.";
      ShutdownFromMixDomain();
      return ZX_ERR_INTERNAL;
    }

    if (last_frame_ref_time > ref_now) {
      // TODO(fxbug.dev/40183): We should not assume anything about fence times for our sources.
      // Instead, we should heed the actual reported fence times (FIFO depth), and the arrivals and
      // departures of sources, and update this number dynamically.
      //
      // Additionally, we must be mindful that if a newly-arriving source causes our "fence time" to
      // increase, we will wake up early. At wakeup time, we need to be able to detect this case and
      // sleep a bit longer before mixing.
      zx::time next_mix_ref_time = last_frame_ref_time + min_fence_time_ + kFenceTimePadding;

      auto mono_wakeup_time = reference_clock().MonotonicTimeFromReferenceTime(next_mix_ref_time);

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

    // Mix the requested number of frames from sources to intermediate buffer, then into output.
    auto buf = mix_stage_->ReadLock(ref_now, frame_count_, mix_state->frames);
    FX_DCHECK(buf);
    FX_DCHECK(buf->start().Floor() == frame_count_);
    FX_DCHECK(buf->length().Floor() > 0);
    FX_DCHECK(static_cast<size_t>(buf->length().Floor()) == mix_state->frames);
    if (!buf) {
      ShutdownFromMixDomain();
      return ZX_ERR_INTERNAL;
    }

    FX_CHECK(output_producer_);
    FX_CHECK(mix_state->target);
    output_producer_->ProduceOutput(reinterpret_cast<float*>(buf->payload()), mix_state->target,
                                    mix_state->frames);

    // Complete this mix job.
    switch (pq->FinishMixerJob(*mix_state)) {
      case CapturePacketQueue::PacketMixStatus::Done:
        // If we filled the entire packet, wake the FIDL thread.
        ready_packets_wakeup_.Signal();
        if (auto s = pq->ReadySize(); s > 0 && s % 20 == 0) {
          FX_LOGS_FIRST_N(WARNING, 100)
              << "Process producing a lot of packets " << s << " frame_count_ " << frame_count_;
        }
        break;

      case CapturePacketQueue::PacketMixStatus::Partial:
        // Did not fill the entire packet yet.
        break;

      case CapturePacketQueue::PacketMixStatus::Discarded:
        // It looks like we were flushed while we were mixing. Invalidate our timeline function,
        // we will re-establish it and flag a discontinuity next time we have work to do.
        ref_clock_to_fractional_dest_frames_->Update(
            TimelineFunction(Fixed(frame_count_).raw_value(), ref_now.get(),
                             fractional_dest_frames_to_ref_clock_rate().Inverse()));
        break;
    }

    // Update the total number of frames we have mixed so far.
    frame_count_ += mix_state->frames;
  }  // while (true)
}

void BaseCapturer::ReportOverflow(zx::time start_time, zx::time end_time) {
  overflow_count_++;

  TRACE_INSTANT("audio", "BaseCapturer::OVERFLOW", TRACE_SCOPE_THREAD);
  if constexpr (kLogCaptureOverflow) {
    auto duration_ms = static_cast<double>((end_time - start_time).to_nsecs()) / ZX_MSEC(1);
    if ((overflow_count_ - 1) % kCaptureOverflowErrorInterval == 0) {
      FX_LOGS(ERROR) << "CAPTURE OVERERFLOW #" << overflow_count_ << " lasted "
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
  // because we're transitioning back to OperatingSync, at which time we'll create
  // an entirely new CapturePacketQueue.
  packet_queue()->DiscardPendingPackets();

  // Invalidate our clock transformation (our next packet will be discontinuous)
  ref_clock_to_fractional_dest_frames_->Update(TimelineFunction());

  // If we had a timer set, make sure that it is canceled. There is no point in
  // having it armed right now as we are in the process of stopping.
  mix_timer_.Cancel();

  // Transition to the AsyncStoppingCallbackPending state, and signal the
  // service thread so it can complete the stop operation.
  UpdateState(State::AsyncStoppingCallbackPending);
  async::PostTask(context_.threading_model().FidlDomain().dispatcher(),
                  [this]() { FinishAsyncStopThunk(); });
}

void BaseCapturer::ShutdownFromMixDomain() {
  TRACE_DURATION("audio", "BaseCapturer::ShutdownFromMixDomain");
  async::PostTask(context_.threading_model().FidlDomain().dispatcher(),
                  [this]() { BeginShutdown(); });
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

  // All done!  Transition back to the OperatingSync state.
  ReportStop();
  UpdateState(State::OperatingSync);
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
      AUDIO_LOG_OBJ(TRACE, this) << "Sync -mode -- payload size:" << pkt.payload_size
                                 << " bytes, offset:" << pkt.payload_offset
                                 << " bytes, flags:" << pkt.flags << ", pts:" << pkt.pts;

      p->callback()(pkt);
    } else {
      AUDIO_LOG_OBJ(TRACE, this) << "Async-mode -- payload size:" << pkt.payload_size
                                 << " bytes, offset:" << pkt.payload_offset
                                 << " bytes, flags:" << pkt.flags << ", pts:" << pkt.pts;

      binding_.events().OnPacketProduced(pkt);

      // TODO(fxbug.dev/43507): Remove this old behavior.
      if (!must_release_packets_) {
        pq->Recycle(pkt);
      }
    }
  }
}

void BaseCapturer::UpdateFormat(Format format) {
  TRACE_DURATION("audio", "BaseCapturer::UpdateFormat");
  // Record our new format.
  FX_DCHECK(state_.load() == State::WaitingForVmo);
  format_ = format;

  reporter().SetFormat(format);

  // Pre-compute the ratio between frames and clock mono ticks. Also figure out
  // the maximum number of frames we are allowed to mix and capture at a time.
  //
  // Some sources (like AudioOutputs) have a limited amount of time which they
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

  // Allocate our MixStage for mixing.
  //
  // TODO(fxbug.dev/39886): Limit this to something smaller than one second of frames.
  uint32_t max_mix_frames = format_.frames_per_second();
  mix_stage_ = std::make_shared<MixStage>(format_, max_mix_frames,
                                          ref_clock_to_fractional_dest_frames_, reference_clock());
}

// For now, we supply the optimal clock as the default: we know it is a clone of MONOTONIC.
// When we switch optimal clock to device clock, the default must still be a clone of MONOTONIC.
// In long-term, use the optimal clock by default.
void BaseCapturer::SetOptimalReferenceClock() {
  TRACE_DURATION("audio", "BaseCapturer::SetOptimalReferenceClock");

  SetClock(AudioClock::CreateAsOptimal(audio::clock::AdjustableCloneOfMonotonic()));

  FX_DCHECK(reference_clock().is_valid()) << "Default reference clock is not valid";
}

// Regardless of the source of the reference clock, we can duplicate and return it here.
void BaseCapturer::GetReferenceClock(GetReferenceClockCallback callback) {
  TRACE_DURATION("audio", "BaseCapturer::GetReferenceClock");
  AUDIO_LOG_OBJ(DEBUG, this);

  auto cleanup = fit::defer([this]() { BeginShutdown(); });

  callback(reference_clock().DuplicateClock());

  cleanup.cancel();
}

}  // namespace media::audio
