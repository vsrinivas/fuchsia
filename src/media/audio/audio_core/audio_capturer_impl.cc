// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_capturer_impl.h"

#include <lib/fit/bridge.h>
#include <lib/fit/defer.h>
#include <lib/media/audio/cpp/types.h>
#include <lib/zx/clock.h>

#include <memory>

#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/audio_core_impl.h"
#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/audio_core/utils.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {
namespace {

// To what extent should client-side under/overflows be logged? (A "client-side underflow" or
// "client-side overflow" refers to when part of a data section is discarded because its start
// timestamp had passed.) For each Capturer, we will log the first overflow. For subsequent
// occurrences, depending on audio_core's logging level, we throttle how frequently these are
// displayed. If log_level is set to TRACE or SPEW, all client-side overflows are logged -- at
// log_level -1: VLOG TRACE -- as specified by kCaptureOverflowTraceInterval. If set to INFO, we
// log less often, at log_level 1: INFO, throttling by factor kCaptureOverflowInfoInterval. If set
// to WARNING or higher, we throttle these even more, specified by kCaptureOverflowErrorInterval.
// To disable all logging of client-side overflows, set kLogCaptureOverflow to false.
//
// Note: by default we set NDEBUG builds to WARNING and DEBUG builds to INFO.
static constexpr bool kLogCaptureOverflow = true;
static constexpr uint16_t kCaptureOverflowTraceInterval = 1;
static constexpr uint16_t kCaptureOverflowInfoInterval = 10;
static constexpr uint16_t kCaptureOverflowErrorInterval = 100;

// Currently, the time we spend mixing must also be taken into account when reasoning about the
// capture fence duration. Today (before any attempt at optimization), a particularly heavy mix
// pass may take longer than 1.5 msec on a DEBUG build(!) on relevant hardware. The constant below
// accounts for this, with additional padding for safety.
const zx::duration kFenceTimePadding = zx::msec(3);

constexpr float kInitialCaptureGainDb = Gain::kUnityGainDb;
constexpr int64_t kMaxTimePerCapture = ZX_MSEC(50);

const Format kInitialFormat =
    Format::Create({.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                    .channels = 1,
                    .frames_per_second = 8000})
        .take_value();

}  // namespace

std::unique_ptr<AudioCapturerImpl> AudioCapturerImpl::Create(
    fuchsia::media::AudioCapturerConfiguration configuration, std::optional<Format> format,
    std::optional<fuchsia::media::AudioCaptureUsage> usage,
    fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request,
    Context* context) {
  return std::unique_ptr<AudioCapturerImpl>(new AudioCapturerImpl(
      std::move(configuration), format, usage, std::move(audio_capturer_request), context));
}

AudioCapturerImpl::AudioCapturerImpl(
    fuchsia::media::AudioCapturerConfiguration configuration, std::optional<Format> format,
    std::optional<fuchsia::media::AudioCaptureUsage> usage,
    fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request, Context* context)
    : AudioObject(Type::AudioCapturer),
      binding_(this, std::move(audio_capturer_request)),
      context_(*context),
      mix_domain_(context_.threading_model().AcquireMixDomain()),
      state_(State::WaitingForVmo),
      loopback_(configuration.is_loopback()),
      min_fence_time_(zx::nsec(0)),
      // Ideally, initialize this to the native configuration of our initially-bound source.
      format_(kInitialFormat),
      stream_gain_db_(kInitialCaptureGainDb),
      mute_(false),
      overflow_count_(0u),
      partial_overflow_count_(0u) {
  FX_DCHECK(mix_domain_);
  REP(AddingCapturer(*this));

  context_.volume_manager().AddStream(this);

  binding_.set_error_handler([this](zx_status_t status) { BeginShutdown(); });
  source_links_.reserve(16u);

  if (usage) {
    usage_ = *usage;
  }

  if (format) {
    UpdateFormat(*format);
  }
}

AudioCapturerImpl::~AudioCapturerImpl() {
  TRACE_DURATION("audio.debug", "AudioCapturerImpl::~AudioCapturerImpl");

  context_.volume_manager().RemoveStream(this);
  REP(RemovingCapturer(*this));
}

void AudioCapturerImpl::ReportStart() {
  context_.audio_admin().UpdateCapturerState(usage_, true, this);
}

void AudioCapturerImpl::ReportStop() {
  context_.audio_admin().UpdateCapturerState(usage_, false, this);
}

void AudioCapturerImpl::OnLinkAdded() {
  context_.volume_manager().NotifyStreamChanged(this);
  RecomputeMinFenceTime();
}

bool AudioCapturerImpl::GetStreamMute() const { return mute_; }

fuchsia::media::Usage AudioCapturerImpl::GetStreamUsage() const {
  fuchsia::media::Usage usage;
  usage.set_capture_usage(usage_);
  return usage;
}

void AudioCapturerImpl::RealizeVolume(VolumeCommand volume_command) {
  if (volume_command.ramp.has_value()) {
    FX_LOGS(WARNING)
        << "Requested ramp of capturer; ramping for destination gains is unimplemented.";
  }

  context_.link_matrix().ForEachSourceLink(*this,
                                           [this, &volume_command](LinkMatrix::LinkHandle link) {
                                             float gain_db = link.loudness_transform->Evaluate<3>({
                                                 VolumeValue{volume_command.volume},
                                                 GainDbFsValue{volume_command.gain_db_adjustment},
                                                 GainDbFsValue{stream_gain_db_.load()},
                                             });

                                             link.mixer->bookkeeping().gain.SetDestGain(gain_db);
                                           });
}

fit::promise<> AudioCapturerImpl::Cleanup() {
  TRACE_DURATION("audio.debug", "AudioCapturerImpl::Cleanup");
  // We need to stop all the async operations happening on the mix dispatcher. These components
  // can only be touched on that thread, so post a task there to run that cleanup.
  fit::bridge<> bridge;
  auto nonce = TRACE_NONCE();
  TRACE_FLOW_BEGIN("audio.debug", "AudioCapturerImpl.capture_cleanup", nonce);
  async::PostTask(mix_domain_->dispatcher(),
                  [this, completer = std::move(bridge.completer), nonce]() mutable {
                    TRACE_DURATION("audio.debug", "AudioCapturerImpl.cleanup_thunk");
                    TRACE_FLOW_END("audio.debug", "AudioCapturerImpl.capture_cleanup", nonce);
                    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, mix_domain_);
                    CleanupFromMixThread();
                    completer.complete_ok();
                  });

  return bridge.consumer.promise();
}

void AudioCapturerImpl::CleanupFromMixThread() {
  TRACE_DURATION("audio", "AudioCapturerImpl::CleanupFromMixThread");
  mix_wakeup_.Deactivate();
  mix_timer_.Cancel();
  mix_domain_ = nullptr;
  state_.store(State::Shutdown);
}

void AudioCapturerImpl::BeginShutdown() {
  context_.threading_model().FidlDomain().ScheduleTask(Cleanup().then([this](fit::result<>&) {
    if (loopback_) {
      context_.route_graph().RemoveLoopbackCapturer(*this);
    } else {
      context_.route_graph().RemoveCapturer(*this);
    }
  }));
}

void AudioCapturerImpl::SetRoutingProfile() {
  auto profile = RoutingProfile{.routable = StateIsRoutable(state_), .usage = GetStreamUsage()};
  if (loopback_) {
    context_.route_graph().SetLoopbackCapturerRoutingProfile(*this, std::move(profile));
  } else {
    context_.route_graph().SetCapturerRoutingProfile(*this, std::move(profile));
  }
}

fit::result<std::shared_ptr<Mixer>, zx_status_t> AudioCapturerImpl::InitializeSourceLink(
    const AudioObject& source, std::shared_ptr<Stream> stream) {
  TRACE_DURATION("audio", "AudioCapturerImpl::InitializeSourceLink");

  switch (state_.load()) {
    // We are operational. Go ahead and add the input to our mix stage.
    case State::OperatingSync:
    case State::OperatingAsync:
    case State::AsyncStopping:
    case State::AsyncStoppingCallbackPending:
      return fit::ok(mix_stage_->AddInput(std::move(stream)));

    // If we are shut down, then I'm not sure why new links are being added, but
    // just go ahead and reject this one. We will be going away shortly.
    case State::Shutdown:
    // If we have not received a VMO yet, then we are still waiting for the user
    // to commit to a format. We should not be establishing links before the
    // capturer is ready.
    case State::WaitingForVmo:
      return fit::error(ZX_ERR_BAD_STATE);
  }
}

void AudioCapturerImpl::CleanupSourceLink(const AudioObject& source,
                                          std::shared_ptr<Stream> stream) {
  mix_stage_->RemoveInput(*stream);
}

void AudioCapturerImpl::GetStreamType(GetStreamTypeCallback cbk) {
  TRACE_DURATION("audio", "AudioCapturerImpl::GetStreamType");
  fuchsia::media::StreamType ret;
  ret.encoding = fuchsia::media::AUDIO_ENCODING_LPCM;
  ret.medium_specific.set_audio(format_.stream_type());
  cbk(std::move(ret));
}

void AudioCapturerImpl::SetPcmStreamType(fuchsia::media::AudioStreamType stream_type) {
  TRACE_DURATION("audio", "AudioCapturerImpl::SetPcmStreamType");
  // If something goes wrong, hang up the phone and shutdown.
  auto cleanup = fit::defer([this]() { BeginShutdown(); });

  // If our shared buffer has been assigned, we are operating and our mode can no longer be changed.
  State state = state_.load();
  if (state != State::WaitingForVmo) {
    FX_LOGS(ERROR) << "Cannot change capture mode while operating!"
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  auto format_result = Format::Create(stream_type);
  if (format_result.is_error()) {
    FX_LOGS(ERROR) << "AudioCapturer: PcmStreamType is invalid";
    return;
  }

  REP(SettingCapturerStreamType(*this, stream_type));

  // Success, record our new format.
  UpdateFormat(format_result.take_value());

  cleanup.cancel();
}

void AudioCapturerImpl::AddPayloadBuffer(uint32_t id, zx::vmo payload_buf_vmo) {
  TRACE_DURATION("audio", "AudioCapturerImpl::AddPayloadBuffer");
  if (id != 0) {
    FX_LOGS(ERROR) << "Only buffer ID 0 is currently supported.";
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
    FX_DCHECK(payload_buf_frames_ != 0);
    FX_LOGS(ERROR) << "Bad state while assigning payload buffer "
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  FX_DCHECK(payload_buf_.start() == nullptr);
  FX_DCHECK(payload_buf_.size() == 0);
  FX_DCHECK(payload_buf_frames_ == 0);

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

  REP(AddingCapturerPayloadBuffer(*this, id, payload_buf_size));

  payload_buf_frames_ = static_cast<uint32_t>(payload_buf_size / format_.bytes_per_frame());
  AUD_VLOG_OBJ(TRACE, this) << "payload buf -- size:" << payload_buf_size
                            << ", frames:" << payload_buf_frames_
                            << ", bytes/frame:" << format_.bytes_per_frame();

  // Allocate our MixStage for mixing.
  //
  // TODO(39886): Limit this to something more reasonable than the entire user-provided VMO.
  mix_stage_ = std::make_shared<MixStage>(format_, payload_buf_frames_,
                                          clock_mono_to_fractional_dest_frames_);

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

  // Success. Although we might still fail to create links to audio sources, we have successfully
  // configured this capturer's mode, so we are now in the OperatingSync state.
  state_.store(State::OperatingSync);

  // Mark ourselves as routable now that we're fully configured.
  FX_DCHECK(context_.link_matrix().SourceLinkCount(*this) == 0u)
      << "No links should be established before a capturer has a payload buffer";
  context_.volume_manager().NotifyStreamChanged(this);
  SetRoutingProfile();
  cleanup.cancel();
}

void AudioCapturerImpl::RemovePayloadBuffer(uint32_t id) {
  TRACE_DURATION("audio", "AudioCapturerImpl::RemovePayloadBuffer");
  FX_LOGS(ERROR) << "RemovePayloadBuffer is not currently supported.";
  BeginShutdown();
}

void AudioCapturerImpl::CaptureAt(uint32_t payload_buffer_id, uint32_t offset_frames,
                                  uint32_t num_frames, CaptureAtCallback cbk) {
  TRACE_DURATION("audio", "AudioCapturerImpl::CaptureAt");
  if (payload_buffer_id != 0) {
    FX_LOGS(ERROR) << "payload_buffer_id must be 0 for now.";
    return;
  }

  // If something goes wrong, hang up the phone and shutdown.
  auto cleanup = fit::defer([this]() { BeginShutdown(); });

  // It is illegal to call CaptureAt unless we are currently operating in
  // synchronous mode.
  State state = state_.load();
  if (state != State::OperatingSync) {
    FX_LOGS(ERROR) << "CaptureAt called while not operating in sync mode "
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  // Buffers submitted by clients must exist entirely within the shared payload buffer, and must
  // have at least some payloads in them.
  uint64_t buffer_end = static_cast<uint64_t>(offset_frames) + num_frames;
  if (!num_frames || (buffer_end > payload_buf_frames_)) {
    FX_LOGS(ERROR) << "Bad buffer range submitted. "
                   << " offset " << offset_frames << " length " << num_frames
                   << ". Shared buffer is " << payload_buf_frames_ << " frames long.";
    return;
  }

  // Allocate bookkeeping to track this pending capture operation.
  auto pending_capture_buffer =
      PendingCaptureBuffer::Allocator::New(offset_frames, num_frames, std::move(cbk));
  if (pending_capture_buffer == nullptr) {
    FX_LOGS(ERROR) << "Failed to allocate pending capture buffer!";
    return;
  }

  // Place the capture operation on the pending list.
  bool wake_mixer;
  {
    std::lock_guard<std::mutex> pending_lock(pending_lock_);
    wake_mixer = pending_capture_buffers_.is_empty();
    pending_capture_buffers_.push_back(std::move(pending_capture_buffer));
  }

  // If the pending list was empty, we need to poke the mixer.
  if (wake_mixer) {
    mix_wakeup_.Signal();
  }
  ReportStart();

  // Things went well. Cancel the cleanup timer and we are done.
  cleanup.cancel();
}

void AudioCapturerImpl::ReleasePacket(fuchsia::media::StreamPacket packet) {
  TRACE_DURATION("audio", "AudioCapturerImpl::ReleasePacket");
  // TODO(43507): Implement.
  FX_LOGS(ERROR) << "ReleasePacket not implemented yet.";
}

void AudioCapturerImpl::DiscardAllPacketsNoReply() {
  TRACE_DURATION("audio", "AudioCapturerImpl::DiscardAllPacketsNoReply");
  DiscardAllPackets(nullptr);
}

void AudioCapturerImpl::DiscardAllPackets(DiscardAllPacketsCallback cbk) {
  TRACE_DURATION("audio", "AudioCapturerImpl::DiscardAllPackets");
  // It is illegal to call Flush unless we are currently operating in
  // synchronous mode.
  State state = state_.load();
  if (state != State::OperatingSync) {
    FX_LOGS(ERROR) << "Flush called while not operating in sync mode "
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    BeginShutdown();
    return;
  }

  // Lock and move the contents of the finished list and pending list to a temporary list. Then
  // deliver the flushed buffers back to the client and send an OnEndOfStream event.
  //
  // Note: the capture thread may currently be mixing frames for the buffer at the head of the
  // pending queue, when the queue is cleared. The fact that these frames were mixed will not be
  // reported to the client; however, the frames will be written to the shared payload buffer.
  PcbList finished;
  {
    std::lock_guard<std::mutex> pending_lock(pending_lock_);
    finished = std::move(finished_capture_buffers_);
    finished.splice(finished.end(), pending_capture_buffers_);
  }

  if (!finished.is_empty()) {
    FinishBuffers(finished);
    binding_.events().OnEndOfStream();
  }

  ReportStop();

  if (cbk != nullptr && binding_.is_bound()) {
    cbk();
  }
}

void AudioCapturerImpl::StartAsyncCapture(uint32_t frames_per_packet) {
  TRACE_DURATION("audio", "AudioCapturerImpl::StartAsyncCapture");
  auto cleanup = fit::defer([this]() { BeginShutdown(); });

  // To enter Async mode, we must be in Synchronous mode and not have pending buffers in flight.
  State state = state_.load();
  if (state != State::OperatingSync) {
    FX_LOGS(ERROR) << "Bad state while attempting to enter async capture mode "
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  bool queues_empty;
  {
    std::lock_guard<std::mutex> pending_lock(pending_lock_);
    queues_empty = pending_capture_buffers_.is_empty() && finished_capture_buffers_.is_empty();
  }

  if (!queues_empty) {
    FX_LOGS(ERROR) << "Attempted to enter async capture mode with capture buffers still in flight.";
    return;
  }

  // Sanity check the number of frames per packet the user is asking for.
  //
  // Currently our minimum frames-per-packet is 1, which is absurdly low.
  // TODO(13344): Decide on a proper minimum packet size, document it, and enforce the limit here.
  if (frames_per_packet == 0) {
    FX_LOGS(ERROR) << "Frames per packet may not be zero.";
    return;
  }

  FX_DCHECK(payload_buf_frames_ > 0);
  if (frames_per_packet > (payload_buf_frames_ / 2)) {
    FX_LOGS(ERROR)
        << "There must be enough room in the shared payload buffer (" << payload_buf_frames_
        << " frames) to fit at least two packets of the requested number of frames per packet ("
        << frames_per_packet << " frames).";
    return;
  }

  // Everything looks good...
  // 1) Record the number of frames per packet we want to produce
  // 2) Transition to the OperatingAsync state
  // 3) Kick the work thread to get the ball rolling.
  async_frames_per_packet_ = frames_per_packet;
  state_.store(State::OperatingAsync);
  ReportStart();
  mix_wakeup_.Signal();
  cleanup.cancel();
}

void AudioCapturerImpl::StopAsyncCaptureNoReply() {
  TRACE_DURATION("audio", "AudioCapturerImpl::StopAsyncCaptureNoReply");
  StopAsyncCapture(nullptr);
}

void AudioCapturerImpl::StopAsyncCapture(StopAsyncCaptureCallback cbk) {
  TRACE_DURATION("audio", "AudioCapturerImpl::StopAsyncCapture");
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
    FX_LOGS(ERROR) << "Bad state while attempting to stop async capture mode "
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    BeginShutdown();
    return;
  }

  // Stash our callback, transition to AsyncStopping, then poke the work thread to shut down.
  FX_DCHECK(pending_async_stop_cbk_ == nullptr);
  pending_async_stop_cbk_ = std::move(cbk);
  ReportStop();
  state_.store(State::AsyncStopping);
  mix_wakeup_.Signal();
}

void AudioCapturerImpl::RecomputeMinFenceTime() {
  TRACE_DURATION("audio", "AudioCapturerImpl::RecomputeMinFenceTime");

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
    FX_VLOGS(TRACE) << "Changing min_fence_time_ (ns) from " << min_fence_time_.get() << " to "
                    << cur_min_fence_time.get();

    REP(SettingCapturerMinFenceTime(*this, cur_min_fence_time));
    min_fence_time_ = cur_min_fence_time;
  }
}

zx_status_t AudioCapturerImpl::Process() {
  TRACE_DURATION("audio", "AudioCapturerImpl::Process");
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

    // Look at the head of the queue, determine our payload buffer position, and get to work.
    void* mix_target = nullptr;
    uint32_t mix_frames;
    uint32_t buffer_sequence_number;
    {
      std::lock_guard<std::mutex> pending_lock(pending_lock_);
      if (!pending_capture_buffers_.is_empty()) {
        auto& p = pending_capture_buffers_.front();

        // This should have been established by CaptureAt; it had better still be true.
        FX_DCHECK((static_cast<uint64_t>(p.offset_frames) + p.num_frames) <= payload_buf_frames_);
        FX_DCHECK(p.filled_frames < p.num_frames);

        // If we don't know our timeline transformation, then the next buffer we produce is
        // guaranteed to be discontinuous relative to the previous one (if any).
        if (!clock_mono_to_fractional_dest_frames_->get().first.invertible()) {
          p.flags |= fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY;
        }

        // If we are running, there is no way our shared buffer can get stolen out from under us.
        FX_DCHECK(payload_buf_.start() != nullptr);

        uint64_t offset_bytes =
            format_.bytes_per_frame() * static_cast<uint64_t>(p.offset_frames + p.filled_frames);

        mix_target = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(payload_buf_.start()) +
                                             offset_bytes);
        mix_frames = p.num_frames - p.filled_frames;
        buffer_sequence_number = p.sequence_number;
      } else {
        if (state_.load() == State::OperatingSync) {
          ReportStop();
        }
      }
    }

    // If there was nothing in our pending capture buffer queue, then one of two things is true:
    //
    // 1) We are operating in synchronous mode and our user is not supplying buffers fast enough.
    // 2) We are starting up in asynchronous mode and have not queued our first buffer yet.
    //
    // Either way, invalidate the frames_to_clock_mono transformation and make sure we don't have a
    // wakeup timer pending. Then, if we are in synchronous mode, simply get out. If we are in
    // asynchronous mode, reset our async ring buffer state, add a new pending capture buffer to the
    // queue, and restart the main Process loop.
    if (mix_target == nullptr) {
      clock_mono_to_fractional_dest_frames_->Update(TimelineFunction());
      frame_count_ = 0;
      mix_timer_.Cancel();

      if (!async_mode) {
        return ZX_OK;
      }

      // If we cannot queue a new pending buffer, it is a fatal error. Simply return instead of
      // trying again, as we are now shutting down.
      async_next_frame_offset_ = 0;
      if (!QueueNextAsyncPendingBuffer()) {
        // If this fails, QueueNextAsyncPendingBuffer should have already shut us down. Assert this.
        FX_DCHECK(state_.load() == State::Shutdown);
        return ZX_ERR_INTERNAL;
      }
      continue;
    }

    // Establish the transform from capture frames to clock monotonic, if we haven't already.
    //
    // Ideally, if there were only one capture source and our frame rates match, we would align our
    // start time exactly with a source sample boundary.
    auto now = zx::clock::get_monotonic();
    if (!clock_mono_to_fractional_dest_frames_->get().first.invertible()) {
      // Ideally a timeline function could alter offsets without also recalculating the scale
      // factor. Then we could re-establish this function without re-reducing the fps-to-nsec rate.
      // Since we supply a rate that is already reduced, this should go pretty quickly.
      clock_mono_to_fractional_dest_frames_->Update(
          TimelineFunction(FractionalFrames<int64_t>(frame_count_).raw_value(), now.get(),
                           fractional_dest_frames_to_clock_mono_rate().Inverse()));
    }

    // Limit our job size to our max job size.
    if (mix_frames > max_frames_per_capture_) {
      mix_frames = max_frames_per_capture_;
    }

    // Figure out when we can finish the job. If in the future, wait until then.
    zx::time last_frame_time =
        zx::time(clock_mono_to_fractional_dest_frames_->get().first.Inverse().Apply(
            FractionalFrames<int64_t>(frame_count_ + mix_frames).raw_value()));
    if (last_frame_time.get() == TimelineRate::kOverflow) {
      FX_LOGS(ERROR) << "Fatal timeline overflow in capture mixer, shutting down capture.";
      ShutdownFromMixDomain();
      return ZX_ERR_INTERNAL;
    }

    if (last_frame_time > now) {
      // TODO(40183): We should not assume anything about fence times for our sources. Instead, we
      // should heed the actual reported fence times (FIFO depth), and the arrivals and departures
      // of sources, and update this number dynamically.
      //
      // Additionally, we must be mindful that if a newly-arriving source causes our "fence time" to
      // increase, we will wake up early. At wakeup time, we need to be able to detect this case and
      // sleep a bit longer before mixing.
      zx::time next_mix_time = last_frame_time + min_fence_time_ + kFenceTimePadding;

      zx_status_t status = mix_timer_.PostForTime(mix_domain_->dispatcher(), next_mix_time);
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "Failed to schedule capturer mix";
        ShutdownFromMixDomain();
        return ZX_ERR_INTERNAL;
      }
      return ZX_OK;
    }

    // Mix the requested number of frames from sources to intermediate buffer, then into output.
    auto buf = mix_stage_->LockBuffer(now, frame_count_, mix_frames);
    FX_DCHECK(buf);
    FX_DCHECK(buf->start().Floor() == frame_count_);
    FX_DCHECK(buf->length().Floor() == mix_frames);
    if (!buf) {
      ShutdownFromMixDomain();
      return ZX_ERR_INTERNAL;
    }

    FX_DCHECK(output_producer_ != nullptr);
    output_producer_->ProduceOutput(reinterpret_cast<float*>(buf->payload()), mix_target,
                                    mix_frames);

    // Update the pending buffer in progress. If finished, return it to the user. If flushed (no
    // pending packet, or queue head was different from what we were working on), just move on.
    bool buffer_finished = false;
    bool wakeup_service_thread = false;
    {
      std::lock_guard<std::mutex> pending_lock(pending_lock_);
      if (!pending_capture_buffers_.is_empty()) {
        auto& p = pending_capture_buffers_.front();
        if (buffer_sequence_number == p.sequence_number) {
          // Update the filled status of the buffer.
          p.filled_frames += mix_frames;
          FX_DCHECK(p.filled_frames <= p.num_frames);

          // Assign a timestamp if one has not already been assigned.
          if (p.capture_timestamp == fuchsia::media::NO_TIMESTAMP) {
            auto [clock_mono_to_fractional_dest_frames, _] =
                clock_mono_to_fractional_dest_frames_->get();
            FX_DCHECK(clock_mono_to_fractional_dest_frames.invertible());
            p.capture_timestamp = clock_mono_to_fractional_dest_frames.Inverse().Apply(
                FractionalFrames<int64_t>(frame_count_).raw_value());
          }

          // If we filled the entire buffer, put it in the queue to be returned to the user.
          buffer_finished = p.filled_frames >= p.num_frames;
          if (buffer_finished) {
            wakeup_service_thread = finished_capture_buffers_.is_empty();
            finished_capture_buffers_.push_back(pending_capture_buffers_.pop_front());
          }
        } else {
          // It looks like we were flushed while we were mixing. Invalidate our timeline function,
          // we will re-establish it and flag a discontinuity next time we have work to do.
          clock_mono_to_fractional_dest_frames_->Update(
              TimelineFunction(FractionalFrames<int64_t>(frame_count_).raw_value(), now.get(),
                               fractional_dest_frames_to_clock_mono_rate().Inverse()));
        }
      }
    }

    // Update the total number of frames we have mixed so far.
    frame_count_ += mix_frames;

    // If we need to poke the service thread, do so.
    if (wakeup_service_thread) {
      async::PostTask(context_.threading_model().FidlDomain().dispatcher(),
                      [this]() { FinishBuffersThunk(); });
    }

    // If in async mode, and we just finished a buffer, queue a new pending buffer (or die trying).
    if (buffer_finished && async_mode && !QueueNextAsyncPendingBuffer()) {
      // If this fails, QueueNextAsyncPendingBuffer should have already shut us down. Assert this.
      FX_DCHECK(state_.load() == State::Shutdown);
      return ZX_ERR_INTERNAL;
    }
  }  // while (true)
}

void AudioCapturerImpl::SetUsage(fuchsia::media::AudioCaptureUsage usage) {
  TRACE_DURATION("audio", "AudioCapturerImpl::SetUsage");
  if (usage == usage_) {
    return;
  }

  ReportStop();
  usage_ = usage;
  context_.volume_manager().NotifyStreamChanged(this);
  State state = state_.load();
  SetRoutingProfile();
  if (state == State::OperatingAsync) {
    ReportStart();
  }
  if (state == State::OperatingSync) {
    std::lock_guard<std::mutex> pending_lock(pending_lock_);
    if (!pending_capture_buffers_.is_empty()) {
      ReportStart();
    }
  }
}

void AudioCapturerImpl::OverflowOccurred(FractionalFrames<int64_t> frac_source_start,
                                         FractionalFrames<int64_t> frac_source_mix_point,
                                         zx::duration overflow_duration) {
  TRACE_INSTANT("audio", "AudioCapturerImpl::OverflowOccurred", TRACE_SCOPE_PROCESS);
  uint16_t overflow_count = std::atomic_fetch_add<uint16_t>(&overflow_count_, 1u);

  if constexpr (kLogCaptureOverflow) {
    auto overflow_msec = static_cast<double>(overflow_duration.to_nsecs()) / ZX_MSEC(1);

    std::ostringstream stream;
    stream << "CAPTURE OVERFLOW #" << overflow_count + 1 << " (1/" << kCaptureOverflowErrorInterval
           << "): source-start " << frac_source_start.raw_value() << " missed mix-point "
           << frac_source_mix_point.raw_value() << " by " << std::setprecision(4) << overflow_msec
           << " ms";

    if ((kCaptureOverflowErrorInterval > 0) &&
        (overflow_count % kCaptureOverflowErrorInterval == 0)) {
      FX_LOGS(ERROR) << stream.str();
    } else if ((kCaptureOverflowInfoInterval > 0) &&
               (overflow_count % kCaptureOverflowInfoInterval == 0)) {
      FX_LOGS(INFO) << stream.str();

    } else if ((kCaptureOverflowTraceInterval > 0) &&
               (overflow_count % kCaptureOverflowTraceInterval == 0)) {
      FX_VLOGS(TRACE) << stream.str();
    }
  }
}

void AudioCapturerImpl::PartialOverflowOccurred(FractionalFrames<int64_t> frac_source_offset,
                                                int64_t dest_mix_offset) {
  TRACE_INSTANT("audio", "AudioCapturerImpl::PartialOverflowOccurred", TRACE_SCOPE_PROCESS);

  // Slips by less than four source frames do not necessarily indicate overflow. A slip of this
  // duration can be caused by the round-to-nearest-dest-frame step, when our rate-conversion
  // ratio is sufficiently large (it can be as large as 4:1).
  if (frac_source_offset.Absolute() >= 4) {
    uint16_t partial_overflow_count = std::atomic_fetch_add<uint16_t>(&partial_overflow_count_, 1u);
    if constexpr (kLogCaptureOverflow) {
      std::ostringstream stream;
      stream << "CAPTURE SLIP #" << partial_overflow_count + 1 << " (1/"
             << kCaptureOverflowErrorInterval << "): shifting by "
             << (frac_source_offset < 0 ? "-0x" : "0x") << std::hex
             << frac_source_offset.Absolute().raw_value() << " source subframes (" << std::dec
             << frac_source_offset.Floor() << " frames) and " << dest_mix_offset
             << " mix (capture) frames";

      if ((kCaptureOverflowErrorInterval > 0) &&
          (partial_overflow_count % kCaptureOverflowErrorInterval == 0)) {
        FX_LOGS(ERROR) << stream.str();
      } else if ((kCaptureOverflowInfoInterval > 0) &&
                 (partial_overflow_count % kCaptureOverflowInfoInterval == 0)) {
        FX_LOGS(INFO) << stream.str();
      } else if ((kCaptureOverflowTraceInterval > 0) &&
                 (partial_overflow_count % kCaptureOverflowTraceInterval == 0)) {
        FX_VLOGS(TRACE) << stream.str();
      }
    }
  } else {
    if constexpr (kLogCaptureOverflow) {
      FX_VLOGS(TRACE) << "Slipping by " << dest_mix_offset
                      << " mix (capture) frames to align with source region";
    }
  }
}

void AudioCapturerImpl::DoStopAsyncCapture() {
  TRACE_DURATION("audio", "AudioCapturerImpl::DoStopAsyncCapture");
  // If this is being called, we had better be in the async stopping state.
  FX_DCHECK(state_.load() == State::AsyncStopping);

  // Finish all pending buffers. We should have at most one pending buffer. Don't bother to move an
  // empty buffer into the finished queue. If there are any buffers in the finished queue waiting to
  // be sent back to the user, make sure that the last one is flagged as the end of stream.
  {
    std::lock_guard<std::mutex> pending_lock(pending_lock_);

    if (!pending_capture_buffers_.is_empty()) {
      auto buf = pending_capture_buffers_.pop_front();

      // When we are in async mode, the Process method will attempt to keep
      // exactly one capture buffer in flight at all times, and never any more.
      // If we just popped that one buffer from the pending queue, we should be
      // able to DCHECK that the queue is now empty.
      FX_CHECK(pending_capture_buffers_.is_empty());

      if (buf->filled_frames > 0) {
        finished_capture_buffers_.push_back(std::move(buf));
      }
    }
  }

  // Invalidate our clock transformation (our next packet will be discontinuous)
  clock_mono_to_fractional_dest_frames_->Update(TimelineFunction());

  // If we had a timer set, make sure that it is canceled. There is no point in
  // having it armed right now as we are in the process of stopping.
  mix_timer_.Cancel();

  // Transition to the AsyncStoppingCallbackPending state, and signal the
  // service thread so it can complete the stop operation.
  state_.store(State::AsyncStoppingCallbackPending);
  async::PostTask(context_.threading_model().FidlDomain().dispatcher(),
                  [this]() { FinishAsyncStopThunk(); });
}

bool AudioCapturerImpl::QueueNextAsyncPendingBuffer() {
  TRACE_DURATION("audio", "AudioCapturerImpl::QueueNextAsyncPendingBuffer");
  // Sanity check our async offset bookkeeping.
  FX_DCHECK(async_next_frame_offset_ < payload_buf_frames_);
  FX_DCHECK(async_frames_per_packet_ <= (payload_buf_frames_ / 2));
  FX_DCHECK(async_next_frame_offset_ <= (payload_buf_frames_ - async_frames_per_packet_));

  // Allocate bookkeeping to track this pending capture operation. If we cannot
  // allocate a new pending capture buffer, it is a fatal error and we need to
  // start the process of shutting down.
  auto pending_capture_buffer = PendingCaptureBuffer::Allocator::New(
      async_next_frame_offset_, async_frames_per_packet_, nullptr);
  if (pending_capture_buffer == nullptr) {
    FX_LOGS(ERROR) << "Failed to allocate pending capture buffer during async capture mode!";
    ShutdownFromMixDomain();
    return false;
  }

  // Update our next frame offset. If the new position of the next frame offset
  // does not leave enough room to produce another contiguous payload for our
  // user, reset the next frame offset to zero. We made sure that we have space
  // for at least two contiguous payload buffers when we started, so the worst
  // case is that we will end up ping-ponging back and forth between two payload
  // buffers located at the start of our shared buffer.
  async_next_frame_offset_ += async_frames_per_packet_;
  uint32_t next_frame_end = async_next_frame_offset_ + async_frames_per_packet_;
  if (next_frame_end > payload_buf_frames_) {
    async_next_frame_offset_ = 0;
  }

  // Queue the pending buffer and signal success.
  {
    std::lock_guard<std::mutex> pending_lock(pending_lock_);
    pending_capture_buffers_.push_back(std::move(pending_capture_buffer));
  }
  return true;
}

void AudioCapturerImpl::ShutdownFromMixDomain() {
  TRACE_DURATION("audio", "AudioCapturerImpl::ShutdownFromMixDomain");
  async::PostTask(context_.threading_model().FidlDomain().dispatcher(),
                  [this]() { BeginShutdown(); });
}

void AudioCapturerImpl::FinishAsyncStopThunk() {
  TRACE_DURATION("audio", "AudioCapturerImpl::FinishAsyncStopThunk");
  // Do nothing if we were shutdown between the time that this message was
  // posted to the main message loop and the time that we were dispatched.
  if (state_.load() == State::Shutdown) {
    return;
  }

  // Start by sending back all of our completed buffers. Finish up by sending
  // an OnEndOfStream event.
  PcbList finished;
  {
    std::lock_guard<std::mutex> pending_lock(pending_lock_);
    FX_DCHECK(pending_capture_buffers_.is_empty());
    finished = std::move(finished_capture_buffers_);
  }

  if (!finished.is_empty()) {
    FinishBuffers(finished);
  }

  binding_.events().OnEndOfStream();

  // If we have a valid callback to make, call it now.
  if (pending_async_stop_cbk_ != nullptr) {
    pending_async_stop_cbk_();
    pending_async_stop_cbk_ = nullptr;
  }

  // All done!  Transition back to the OperatingSync state.
  ReportStop();
  state_.store(State::OperatingSync);
}

void AudioCapturerImpl::FinishBuffersThunk() {
  TRACE_DURATION("audio", "AudioCapturerImpl::FinishBuffersThunk");
  // Do nothing if we were shutdown between the time that this message was
  // posted to the main message loop and the time that we were dispatched.
  if (state_.load() == State::Shutdown) {
    return;
  }

  PcbList finished;
  {
    std::lock_guard<std::mutex> pending_lock(pending_lock_);
    finished = std::move(finished_capture_buffers_);
  }

  FinishBuffers(finished);
}

void AudioCapturerImpl::FinishBuffers(const PcbList& finished_buffers) {
  TRACE_DURATION("audio", "AudioCapturerImpl::FinishBuffers");
  for (const auto& finished_buffer : finished_buffers) {
    // If there is no callback tied to this buffer (meaning that it was generated while operating in
    // async mode), and it is not filled at all, just skip it.
    if ((finished_buffer.cbk == nullptr) && !finished_buffer.filled_frames) {
      continue;
    }

    fuchsia::media::StreamPacket pkt;

    pkt.pts = finished_buffer.capture_timestamp;
    pkt.flags = finished_buffer.flags;
    pkt.payload_buffer_id = 0u;
    pkt.payload_offset = finished_buffer.offset_frames * format_.bytes_per_frame();
    pkt.payload_size = finished_buffer.filled_frames * format_.bytes_per_frame();

    REP(SendingCapturerPacket(*this, pkt));

    if (finished_buffer.cbk != nullptr) {
      AUD_VLOG_OBJ(SPEW, this) << "Sync -mode -- payload size:" << pkt.payload_size
                               << " bytes, offset:" << pkt.payload_offset
                               << " bytes, flags:" << pkt.flags << ", pts:" << pkt.pts;

      finished_buffer.cbk(pkt);
    } else {
      AUD_VLOG_OBJ(SPEW, this) << "Async-mode -- payload size:" << pkt.payload_size
                               << " bytes, offset:" << pkt.payload_offset
                               << " bytes, flags:" << pkt.flags << ", pts:" << pkt.pts;

      binding_.events().OnPacketProduced(pkt);
    }
  }
}

void AudioCapturerImpl::UpdateFormat(Format format) {
  TRACE_DURATION("audio", "AudioCapturerImpl::UpdateFormat");
  // Record our new format.
  FX_DCHECK(state_.load() == State::WaitingForVmo);
  format_ = format;

  // Pre-compute the ratio between frames and clock mono ticks. Also figure out
  // the maximum number of frames we are allowed to mix and capture at a time.
  //
  // Some sources (like AudioOutputs) have a limited amount of time which they
  // are able to hold onto data after presentation. We need to wait until after
  // presentation time to capture these frames, but if we batch up too much
  // work, then the AudioOutput may have overwritten the data before we decide
  // to get around to capturing it. Limiting our maximum number of frames of to
  // capture to be less than this amount of time prevents this issue.
  int64_t tmp;
  tmp = dest_frames_to_clock_mono_rate().Inverse().Scale(kMaxTimePerCapture);
  max_frames_per_capture_ = static_cast<uint32_t>(tmp);

  FX_DCHECK(tmp <= std::numeric_limits<uint32_t>::max());
  FX_DCHECK(max_frames_per_capture_ > 0);
}

void AudioCapturerImpl::BindGainControl(
    fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) {
  TRACE_DURATION("audio", "AudioCapturerImpl::BindGainControl");
  gain_control_bindings_.AddBinding(this, std::move(request));
}

void AudioCapturerImpl::SetGain(float gain_db) {
  TRACE_DURATION("audio", "AudioCapturerImpl::SetGain");
  // Before setting stream_gain_db_, we should always perform this range check.
  if ((gain_db < fuchsia::media::audio::MUTED_GAIN_DB) ||
      (gain_db > fuchsia::media::audio::MAX_GAIN_DB) || isnan(gain_db)) {
    FX_LOGS(ERROR) << "SetGain(" << gain_db << " dB) out of range.";
    BeginShutdown();
    return;
  }

  // If the incoming SetGain request represents no change, we're done
  // (once we add gain ramping, this type of check isn't workable).
  if (stream_gain_db_ == gain_db) {
    return;
  }

  REP(SettingCapturerGain(*this, gain_db));

  stream_gain_db_.store(gain_db);
  context_.volume_manager().NotifyStreamChanged(this);

  NotifyGainMuteChanged();
}

void AudioCapturerImpl::SetMute(bool mute) {
  TRACE_DURATION("audio", "AudioCapturerImpl::SetMute");
  // If the incoming SetMute request represents no change, we're done.
  if (mute_ == mute) {
    return;
  }

  REP(SettingCapturerMute(*this, mute));

  mute_ = mute;

  context_.volume_manager().NotifyStreamChanged(this);
  NotifyGainMuteChanged();
}

void AudioCapturerImpl::NotifyGainMuteChanged() {
  TRACE_DURATION("audio", "AudioCapturerImpl::NotifyGainMuteChanged");
  // Consider making these events disable-able like MinLeadTime.
  for (auto& gain_binding : gain_control_bindings_.bindings()) {
    gain_binding->events().OnGainMuteChanged(stream_gain_db_, mute_);
  }
}

}  // namespace media::audio
