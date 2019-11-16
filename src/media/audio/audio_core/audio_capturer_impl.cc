// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_capturer_impl.h"

#include <lib/fit/bridge.h>
#include <lib/fit/defer.h>
#include <lib/media/audio/cpp/types.h>
#include <lib/zx/clock.h>

#include <memory>

#include "src/media/audio/audio_core/audio_core_impl.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/audio_core/utils.h"
#include "src/media/audio/lib/logging/logging.h"

// Allow (at most) 256 slabs of pending capture buffers. At 16KB per slab, this
// means we will deny allocations after 4MB. If we ever need more than 4MB of
// pending capture buffer bookkeeping, something has gone seriously wrong.
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(media::audio::AudioCapturerImpl::PcbAllocatorTraits, 0x100);

namespace media::audio {

constexpr bool VERBOSE_TIMING_DEBUG = false;

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

// static
AtomicGenerationId AudioCapturerImpl::PendingCaptureBuffer::sequence_generator;

fbl::RefPtr<AudioCapturerImpl> AudioCapturerImpl::Create(
    bool loopback, fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request,
    AudioCoreImpl* owner) {
  return fbl::AdoptRef(new AudioCapturerImpl(loopback, std::move(audio_capturer_request),
                                             &owner->threading_model(), &owner->route_graph(),
                                             &owner->audio_admin(), &owner->volume_manager()));
}

fbl::RefPtr<AudioCapturerImpl> AudioCapturerImpl::Create(
    bool loopback, fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request,
    ThreadingModel* threading_model, RouteGraph* route_graph, AudioAdmin* admin,
    StreamVolumeManager* volume_manager) {
  return fbl::AdoptRef(new AudioCapturerImpl(loopback, std::move(audio_capturer_request),
                                             threading_model, route_graph, admin, volume_manager));
}

AudioCapturerImpl::AudioCapturerImpl(
    bool loopback, fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request,
    ThreadingModel* threading_model, RouteGraph* route_graph, AudioAdmin* admin,
    StreamVolumeManager* volume_manager)
    : AudioObject(Type::AudioCapturer),
      usage_(fuchsia::media::AudioCaptureUsage::FOREGROUND),
      binding_(this, std::move(audio_capturer_request)),
      threading_model_(*threading_model),
      mix_domain_(threading_model_.AcquireMixDomain()),
      admin_(*admin),
      volume_manager_(*volume_manager),
      route_graph_(*route_graph),
      state_(State::WaitingForVmo),
      loopback_(loopback),
      min_fence_time_(zx::nsec(0)),
      stream_gain_db_(kInitialCaptureGainDb),
      mute_(false),
      overflow_count_(0u),
      partial_overflow_count_(0u) {
  FX_DCHECK(route_graph);
  FX_DCHECK(admin);
  FX_DCHECK(mix_domain_);
  REP(AddingCapturer(*this));

  std::vector<fuchsia::media::AudioCaptureUsage> allowed_usages;
  allowed_usages.push_back(fuchsia::media::AudioCaptureUsage::FOREGROUND);
  allowed_usages.push_back(fuchsia::media::AudioCaptureUsage::BACKGROUND);
  allowed_usages.push_back(fuchsia::media::AudioCaptureUsage::COMMUNICATION);
  allowed_usages.push_back(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
  allowed_usages_ = std::move(allowed_usages);

  volume_manager_.AddStream(this);

  binding_.set_error_handler([this](zx_status_t status) { RemoveFromRouteGraph(); });
  source_link_refs_.reserve(16u);

  // Ideally, initialize this to the native configuration of our initially-bound source.
  format_ = fuchsia::media::AudioStreamType::New();
  UpdateFormat(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 8000);
}

AudioCapturerImpl::~AudioCapturerImpl() {
  TRACE_DURATION("audio.debug", "AudioCapturerImpl::~AudioCapturerImpl");

  volume_manager_.RemoveStream(this);
  REP(RemovingCapturer(*this));

  FX_DCHECK(!payload_buf_vmo_.is_valid());
  FX_DCHECK(payload_buf_virt_ == nullptr);
}

void AudioCapturerImpl::ReportStart() { admin_.UpdateCapturerState(usage_, true, this); }

void AudioCapturerImpl::ReportStop() { admin_.UpdateCapturerState(usage_, false, this); }

void AudioCapturerImpl::OnLinkAdded() {
  volume_manager_.NotifyStreamChanged(this);
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

  ForEachSourceLink([stream_gain_db = stream_gain_db_.load(), &volume_command](auto& link) {
    // Gain objects contain multiple stages. In capture, device gain is
    // the "source" stage and stream gain is the "dest" stage.
    float gain_db = link.volume_curve().VolumeToDb(volume_command.volume);

    gain_db = Gain::CombineGains(gain_db, stream_gain_db);
    gain_db = Gain::CombineGains(gain_db, volume_command.gain_db_adjustment);

    link.gain().SetDestGain(gain_db);
  });
}

void AudioCapturerImpl::SetInitialFormat(fuchsia::media::AudioStreamType format) {
  TRACE_DURATION("audio", "AudioCapturerImpl::SetInitialFormat");
  UpdateFormat(format.sample_format, format.channels, format.frames_per_second);
}

void AudioCapturerImpl::Shutdown(std::unique_ptr<AudioCapturerImpl> self) {
  TRACE_DURATION("audio", "AudioCapturerImpl::Shutdown");
  ReportStop();

  threading_model_.FidlDomain().ScheduleTask(
      Cleanup().then([self = std::move(self)](fit::result<>&) {
        // Release our buffer resources.
        //
        // It's important that we don't release the buffer until the mix thread cleanup has run as
        // the mixer could still be accessing the memory backing the buffer.
        //
        // TODO(mpuryear): Change AudioCapturer to use the RingBuffer utility class.
        if (self->payload_buf_virt_ != nullptr) {
          FX_DCHECK(self->payload_buf_size_ != 0);
          zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(self->payload_buf_virt_),
                                       self->payload_buf_size_);
          self->payload_buf_virt_ = nullptr;
          self->payload_buf_size_ = 0;
          self->payload_buf_frames_ = 0;
        }

        self->payload_buf_vmo_.reset();
      }));
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

void AudioCapturerImpl::RemoveFromRouteGraph() {
  if (loopback_) {
    route_graph_.RemoveLoopbackCapturer(this);
  } else {
    route_graph_.RemoveCapturer(this);
  }
}

void AudioCapturerImpl::fbl_recycle() {
  // recycle gives us `this` to free ourselves. At this point, there are no other references to us.
  //
  // It is therefore safe for us to take ownership of ourselves until all our shared resources are
  // cleaned up and shut down.
  Shutdown(std::unique_ptr<AudioCapturerImpl>(this));
}

zx_status_t AudioCapturerImpl::InitializeSourceLink(const fbl::RefPtr<AudioLink>& link) {
  TRACE_DURATION("audio", "AudioCapturerImpl::InitializeSourceLink");

  // Choose a mixer
  switch (state_.load()) {
    // We are operational. Go ahead and choose a mixer.
    case State::OperatingSync:
    case State::OperatingAsync:
    case State::AsyncStopping:
    case State::AsyncStoppingCallbackPending:
      return ChooseMixer(link);

    // If we are shut down, then I'm not sure why new links are being added, but
    // just go ahead and reject this one. We will be going away shortly.
    case State::Shutdown:
    // If we have not received a VMO yet, then we are still waiting for the user
    // to commit to a format. We should not be establishing links before the
    // capturer is ready.
    case State::WaitingForVmo:
      return ZX_ERR_BAD_STATE;
  }
}

void AudioCapturerImpl::GetStreamType(GetStreamTypeCallback cbk) {
  TRACE_DURATION("audio", "AudioCapturerImpl::GetStreamType");
  fuchsia::media::StreamType ret;
  ret.encoding = fuchsia::media::AUDIO_ENCODING_LPCM;
  ret.medium_specific.set_audio(*format_);
  cbk(std::move(ret));
}

void AudioCapturerImpl::SetPcmStreamType(fuchsia::media::AudioStreamType stream_type) {
  TRACE_DURATION("audio", "AudioCapturerImpl::SetPcmStreamType");
  // If something goes wrong, hang up the phone and shutdown.
  auto cleanup = fit::defer([this]() { RemoveFromRouteGraph(); });

  // If our shared buffer has been assigned, we are operating and our mode can no longer be changed.
  State state = state_.load();
  if (state != State::WaitingForVmo) {
    FX_DCHECK(payload_buf_vmo_.is_valid());
    FX_LOGS(ERROR) << "Cannot change capture mode while operating!"
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  // Sanity check the details of the mode request.
  if ((stream_type.channels < fuchsia::media::MIN_PCM_CHANNEL_COUNT) ||
      (stream_type.channels > fuchsia::media::MAX_PCM_CHANNEL_COUNT)) {
    FX_LOGS(ERROR) << "Bad channel count, " << stream_type.channels << " is not in the range ["
                   << fuchsia::media::MIN_PCM_CHANNEL_COUNT << ", "
                   << fuchsia::media::MAX_PCM_CHANNEL_COUNT << "]";
    return;
  }

  if ((stream_type.frames_per_second < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) ||
      (stream_type.frames_per_second > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)) {
    FX_LOGS(ERROR) << "Bad frame rate, " << stream_type.frames_per_second
                   << " is not in the range [" << fuchsia::media::MIN_PCM_FRAMES_PER_SECOND << ", "
                   << fuchsia::media::MAX_PCM_FRAMES_PER_SECOND << "]";
    return;
  }

  switch (stream_type.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
    case fuchsia::media::AudioSampleFormat::FLOAT:
      break;

    default:
      FX_LOGS(ERROR) << "Bad sample format " << fidl::ToUnderlying(stream_type.sample_format);
      return;
  }

  REP(SettingCapturerStreamType(*this, stream_type));

  // Success, record our new format.
  UpdateFormat(stream_type.sample_format, stream_type.channels, stream_type.frames_per_second);

  cleanup.cancel();
}

void AudioCapturerImpl::AddPayloadBuffer(uint32_t id, zx::vmo payload_buf_vmo) {
  TRACE_DURATION("audio", "AudioCapturerImpl::AddPayloadBuffer");
  if (id != 0) {
    FX_LOGS(ERROR) << "Only buffer ID 0 is currently supported.";
    RemoveFromRouteGraph();
    return;
  }

  FX_DCHECK(payload_buf_vmo.is_valid());

  // If something goes wrong, hang up the phone and shutdown.
  auto cleanup = fit::defer([this]() { RemoveFromRouteGraph(); });
  zx_status_t res;

  State state = state_.load();
  if (state != State::WaitingForVmo) {
    FX_DCHECK(payload_buf_vmo_.is_valid());
    FX_DCHECK(payload_buf_virt_ != nullptr);
    FX_DCHECK(payload_buf_size_ != 0);
    FX_DCHECK(payload_buf_frames_ != 0);
    FX_LOGS(ERROR) << "Bad state while assigning payload buffer "
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  FX_DCHECK(payload_buf_virt_ == nullptr);
  FX_DCHECK(payload_buf_size_ == 0);
  FX_DCHECK(payload_buf_frames_ == 0);

  // Take ownership of the VMO, fetch and sanity check the size.
  payload_buf_vmo_ = std::move(payload_buf_vmo);
  res = payload_buf_vmo_.get_size(&payload_buf_size_);
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Failed to fetch payload buffer VMO size";
    return;
  }

  FX_CHECK(bytes_per_frame_ > 0);
  constexpr uint64_t max_uint32 = std::numeric_limits<uint32_t>::max();
  if ((payload_buf_size_ < bytes_per_frame_) ||
      (payload_buf_size_ > (max_uint32 * bytes_per_frame_))) {
    FX_LOGS(ERROR) << "Bad payload buffer VMO size (size = " << payload_buf_size_
                   << ", bytes per frame = " << bytes_per_frame_ << ")";
    return;
  }

  REP(AddingCapturerPayloadBuffer(*this, id, payload_buf_size_));

  payload_buf_frames_ = static_cast<uint32_t>(payload_buf_size_ / bytes_per_frame_);
  AUD_VLOG_OBJ(TRACE, this) << "payload buf -- size:" << payload_buf_size_
                            << ", frames:" << payload_buf_frames_
                            << ", bytes/frame:" << bytes_per_frame_;

  // Allocate our intermediate buffer for mixing.
  //
  // TODO(39886): Limit this to something more reasonable than the entire user-provided VMO.
  mix_buf_ = std::make_unique<float[]>(payload_buf_frames_);

  // Map the VMO into our process.
  uintptr_t tmp;
  res = zx::vmar::root_self()->map(0, payload_buf_vmo_, 0, payload_buf_size_,
                                   ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &tmp);
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Failed to map payload buffer VMO";
    return;
  }

  payload_buf_virt_ = reinterpret_cast<void*>(tmp);

  // Activate the dispatcher primitives we will use to drive the mixing process. Note we must call
  // Activate on the WakeupEvent from the mix domain, but Signal can be called anytime, even before
  // this Activate occurs.
  async::PostTask(mix_domain_->dispatcher(), [self = fbl::RefPtr(this)] {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, self->mix_domain_);
    zx_status_t status =
        self->mix_wakeup_.Activate(self->mix_domain_->dispatcher(),
                                   [self = std::move(self)](WakeupEvent* event) -> zx_status_t {
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
  output_producer_ = OutputProducer::Select(format_);
  if (output_producer_ == nullptr) {
    FX_LOGS(ERROR) << "Failed to select output producer";
    return;
  }

  // Success. Although we might still fail to create links to audio sources, we have successfully
  // configured this capturer's mode, so we are now in the OperatingSync state.
  state_.store(State::OperatingSync);

  // Mark ourselves as routable now that we're fully configured.
  FX_DCHECK(source_link_count() == 0)
      << "No links should be established before a capturer has a payload buffer";
  volume_manager_.NotifyStreamChanged(this);
  if (loopback_) {
    route_graph_.SetLoopbackCapturerRoutingProfile(this, {.routable = true});
  } else {
    route_graph_.SetCapturerRoutingProfile(this, {.routable = true});
  }

  cleanup.cancel();
}

void AudioCapturerImpl::RemovePayloadBuffer(uint32_t id) {
  TRACE_DURATION("audio", "AudioCapturerImpl::RemovePayloadBuffer");
  FX_LOGS(ERROR) << "RemovePayloadBuffer is not currently supported.";
  RemoveFromRouteGraph();
}

void AudioCapturerImpl::CaptureAt(uint32_t payload_buffer_id, uint32_t offset_frames,
                                  uint32_t num_frames, CaptureAtCallback cbk) {
  TRACE_DURATION("audio", "AudioCapturerImpl::CaptureAt");
  if (payload_buffer_id != 0) {
    FX_LOGS(ERROR) << "payload_buffer_id must be 0 for now.";
    return;
  }

  // If something goes wrong, hang up the phone and shutdown.
  auto cleanup = fit::defer([this]() { RemoveFromRouteGraph(); });

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
  auto pending_capture_buffer = PcbAllocator::New(offset_frames, num_frames, std::move(cbk));
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
  // TODO(mpuryear): Implement.
  FX_LOGS(ERROR) << "ReleasePacket not implemented yet.";
  RemoveFromRouteGraph();
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
    RemoveFromRouteGraph();
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
  auto cleanup = fit::defer([this]() { RemoveFromRouteGraph(); });

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
    RemoveFromRouteGraph();
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
  ForEachSourceLink([&cur_min_fence_time](auto& source_link) {
    if (source_link.GetSource()->is_input()) {
      const auto device = fbl::RefPtr<AudioDevice>::Downcast(source_link.GetSource());
      auto fence_time = device->driver()->fifo_depth_duration();

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

struct RbRegion {
  uint32_t srb_pos;   // start ring buffer pos
  uint32_t len;       // region length in frames
  int64_t sfrac_pts;  // start fractional frame pts
};

// Utility functions to debug clocking in MixToIntermediate.
//
// Display ring-buffer region info.
void DumpRbRegions(const RbRegion* regions) {
  for (auto i = 0; i < 2; ++i) {
    if (regions[i].len) {
      AUD_VLOG(SPEW) << "[" << i << "] srb_pos 0x" << std::hex << regions[i].srb_pos << ", len 0x"
                     << regions[i].len << ", sfrac_pts 0x" << regions[i].sfrac_pts << " ("
                     << std::dec << (regions[i].sfrac_pts >> kPtsFractionalBits) << " frames)";
    } else {
      AUD_VLOG(SPEW) << "[" << i << "] len 0x0";
    }
  }
}

// Display a timeline function.
void DumpTimelineFunction(const media::TimelineFunction& timeline_function) {
  FX_VLOGS(SPEW) << "(TLFunction) sub/ref deltas " << timeline_function.subject_delta() << "/"
                 << timeline_function.reference_delta() << ", sub/ref times "
                 << timeline_function.subject_time() << "/" << timeline_function.reference_time();
}

// Display a ring-buffer snapshot.
void DumpRbSnapshot(const AudioDriver::RingBufferSnapshot& rb_snap) {
  AUD_VLOG_OBJ(SPEW, &rb_snap) << "(RBSnapshot) position_to_end_fence_frames "
                               << rb_snap.position_to_end_fence_frames
                               << ", end_fence_to_start_fence_frames "
                               << rb_snap.end_fence_to_start_fence_frames << ", gen_id "
                               << rb_snap.gen_id;

  FX_VLOGS(SPEW) << "rb_snap.clock_mono_to_ring_pos_bytes:";
  DumpTimelineFunction(rb_snap.clock_mono_to_ring_pos_bytes);

  auto rb = rb_snap.ring_buffer;
  AUD_VLOG_OBJ(SPEW, rb_snap.ring_buffer.get())
      << "(DriverRBuf) size " << rb->size() << ", frames " << rb->frames() << ", frame_size "
      << rb->frame_size() << ", start " << static_cast<void*>(rb->virt());
}

// Display a mixer bookkeeping struct.
void DumpMixer(const Mixer& mixer) {
  auto& mix_state = mixer.bookkeeping();
  AUD_VLOG_OBJ(SPEW, &mix_state) << "(Bookkeep) mixer " << &mixer << " gain " << &mix_state.gain
                                 << ", step_size x" << std::hex << mix_state.step_size
                                 << ", rate_mod/den " << std::dec << mix_state.rate_modulo << "/"
                                 << mix_state.denominator << " src_pos_mod "
                                 << mix_state.src_pos_modulo << ", src_trans_gen "
                                 << mix_state.source_trans_gen_id << ", dest_trans_gen "
                                 << mix_state.dest_trans_gen_id;

  FX_VLOGS(SPEW) << "mix_state.dest_frames_to_frac_source_frames:";
  DumpTimelineFunction(mix_state.dest_frames_to_frac_source_frames);

  FX_VLOGS(SPEW) << "mix_state.clock_mono_to_frac_source_frames:";
  DumpTimelineFunction(mix_state.clock_mono_to_frac_source_frames);
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
        if (!dest_frames_to_clock_mono_.invertible()) {
          p.flags |= fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY;
        }

        // If we are running, there is no way our shared buffer can get stolen out from under us.
        FX_DCHECK(payload_buf_virt_ != nullptr);

        uint64_t offset_bytes =
            bytes_per_frame_ * static_cast<uint64_t>(p.offset_frames + p.filled_frames);

        mix_target =
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(payload_buf_virt_) + offset_bytes);
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
      dest_frames_to_clock_mono_ = TimelineFunction();
      dest_frames_to_clock_mono_gen_.Next();
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
    if (!dest_frames_to_clock_mono_.invertible()) {
      // Ideally a timeline function could alter offsets without also recalculating the scale
      // factor. Then we could re-establish this function without re-reducing the fps-to-nsec rate.
      // Since we supply a rate that is already reduced, this should go pretty quickly.
      dest_frames_to_clock_mono_ =
          TimelineFunction(now.get(), frame_count_, dest_frames_to_clock_mono_rate_);
      dest_frames_to_clock_mono_gen_.Next();
      FX_DCHECK(dest_frames_to_clock_mono_.invertible());
    }

    // Limit our job size to our max job size.
    if (mix_frames > max_frames_per_capture_) {
      mix_frames = max_frames_per_capture_;
    }

    // Figure out when we can finish the job. If in the future, wait until then.
    zx::time last_frame_time =
        zx::time(dest_frames_to_clock_mono_.Apply(frame_count_ + mix_frames));
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
    if (!MixToIntermediate(mix_frames)) {
      ShutdownFromMixDomain();
      return ZX_ERR_INTERNAL;
    }

    FX_DCHECK(output_producer_ != nullptr);
    output_producer_->ProduceOutput(mix_buf_.get(), mix_target, mix_frames);

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
            FX_DCHECK(dest_frames_to_clock_mono_.invertible());
            p.capture_timestamp = dest_frames_to_clock_mono_.Apply(frame_count_);
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
          dest_frames_to_clock_mono_ =
              TimelineFunction(now.get(), frame_count_, dest_frames_to_clock_mono_rate_);
          dest_frames_to_clock_mono_gen_.Next();
        }
      }
    }

    // Update the total number of frames we have mixed so far.
    frame_count_ += mix_frames;

    // If we need to poke the service thread, do so.
    if (wakeup_service_thread) {
      async::PostTask(threading_model_.FidlDomain().dispatcher(),
                      [thiz = fbl::RefPtr(this)]() { thiz->FinishBuffersThunk(); });
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
  for (auto allowed : allowed_usages_) {
    if (allowed == usage) {
      ReportStop();
      usage_ = usage;
      volume_manager_.NotifyStreamChanged(this);
      State state = state_.load();
      if (state == State::OperatingAsync) {
        ReportStart();
      }
      if (state == State::OperatingSync) {
        std::lock_guard<std::mutex> pending_lock(pending_lock_);
        if (!pending_capture_buffers_.is_empty()) {
          ReportStart();
        }
      }
      return;
    }
  }
  FX_LOGS(ERROR) << "Disallowed or unknown usage - terminating the stream";
  RemoveFromRouteGraph();
}

void AudioCapturerImpl::OverflowOccurred(int64_t frac_source_start, int64_t frac_source_mix_point,
                                         zx::duration overflow_duration) {
  TRACE_INSTANT("audio", "AudioCapturerImpl::OverflowOccurred", TRACE_SCOPE_PROCESS);
  uint16_t overflow_count = std::atomic_fetch_add<uint16_t>(&overflow_count_, 1u);

  if constexpr (kLogCaptureOverflow) {
    auto overflow_msec = static_cast<double>(overflow_duration.to_nsecs()) / ZX_MSEC(1);

    std::ostringstream stream;
    stream << "CAPTURE OVERFLOW #" << overflow_count + 1 << " (1/" << kCaptureOverflowErrorInterval
           << "): source-start " << frac_source_start << " missed mix-point "
           << frac_source_mix_point << " by " << std::setprecision(4) << overflow_msec << " ms";

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

void AudioCapturerImpl::PartialOverflowOccurred(int64_t frac_source_offset,
                                                int64_t dest_mix_offset) {
  TRACE_INSTANT("audio", "AudioCapturerImpl::PartialOverflowOccurred", TRACE_SCOPE_PROCESS);

  // Slips by less than four source frames do not necessarily indicate overflow. A slip of this
  // duration can be caused by the round-to-nearest-dest-frame step, when our rate-conversion
  // ratio is sufficiently large (it can be as large as 4:1).
  if (abs(frac_source_offset) >= (Mixer::FRAC_ONE << 2)) {
    uint16_t partial_overflow_count = std::atomic_fetch_add<uint16_t>(&partial_overflow_count_, 1u);
    if constexpr (kLogCaptureOverflow) {
      std::ostringstream stream;
      stream << "CAPTURE SLIP #" << partial_overflow_count + 1 << " (1/"
             << kCaptureOverflowErrorInterval << "): shifting by "
             << (frac_source_offset < 0 ? "-0x" : "0x") << std::hex << abs(frac_source_offset)
             << " source subframes (" << std::dec << (frac_source_offset >> kPtsFractionalBits)
             << " frames) and " << dest_mix_offset << " mix (capture) frames";

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

bool AudioCapturerImpl::MixToIntermediate(uint32_t mix_frames) {
  TRACE_DURATION("audio", "AudioCapturerImpl::MixToIntermediate");
  // Snapshot our source link references, but skip packet sources (we can't sample from them yet).
  FX_DCHECK(source_link_refs_.size() == 0);

  ForEachSourceLink([src_link_refs = &source_link_refs_](auto& link) {
    src_link_refs->emplace_back(fbl::RefPtr(&link));
  });

  // No matter what happens here, make certain that we are not holding any link
  // references in our snapshot when we are done.
  //
  // Note: We need to disable the clang static thread analysis code with this
  // lambda because clang is not able to know that...
  // 1) Once placed within the fit::defer, this cleanup routine cannot be
  //    transferred out of the scope of the MixToIntermediate function (so its
  //    life is bound to the scope of this function).
  // 2) Because of this, the defer basically should inherit all of the thread
  //    analysis attributes of MixToIntermediate, including the assertion that
  //    MixToIntermediate is running in the mixer execution domain, which is
  //    what guards the source_link_refs_ member.
  // For this reason, we manually disable thread analysis on the cleanup lambda.
  auto release_snapshot_refs =
      fit::defer([this]() FXL_NO_THREAD_SAFETY_ANALYSIS { source_link_refs_.clear(); });

  // Silence our intermediate buffer.
  size_t job_bytes = sizeof(mix_buf_[0]) * mix_frames * format_->channels;
  std::memset(mix_buf_.get(), 0u, job_bytes);

  // If our capturer is mute, we have nothing to do after filling with silence.
  if (mute_ || (stream_gain_db_.load() <= fuchsia::media::audio::MUTED_GAIN_DB)) {
    return true;
  }

  bool accumulate = false;
  for (auto& link : source_link_refs_) {
    FX_DCHECK(link->GetSource()->is_input() || link->GetSource()->is_output());

    // Get a hold of our device source (we know it is a device because this is a
    // ring buffer source, and ring buffer sources are always currently input
    // devices) and snapshot the current state of the ring buffer.
    FX_DCHECK(link->GetSource() != nullptr);
    auto& device = static_cast<AudioDevice&>(*link->GetSource());

    // TODO(MTWN-52): Right now, the only device without a driver is the throttle output. Sourcing a
    // capturer from the throttle output would be a mistake. For now if we detect this, log a
    // warning, signal error and shut down. Once this is resolved, come back and remove this.
    const auto& driver = device.driver();
    if (driver == nullptr) {
      FX_LOGS(ERROR) << "AudioCapturer appears to be linked to throttle output! Shutting down";
      return false;
    }

    // Get our capture link mixer.
    FX_DCHECK(link->mixer() != nullptr);
    auto& mixer = static_cast<Mixer&>(*link->mixer());
    auto& info = mixer.bookkeeping();

    // If this gain scale is at or below our mute threshold, skip this source,
    // as it will not contribute to this mix pass.
    if (info.gain.IsSilent()) {
      AUD_LOG_OBJ(INFO, &link) << "Skipping this capture source -- it is mute";
      continue;
    }

    AudioDriver::RingBufferSnapshot rb_snap;
    driver->SnapshotRingBuffer(&rb_snap);

    // If a driver does not have its ring buffer, or a valid clock monotonic to
    // ring buffer position transformation, then there is nothing to do (at the
    // moment). Just skip this source and move on to the next one.
    if ((rb_snap.ring_buffer == nullptr) || (!rb_snap.clock_mono_to_ring_pos_bytes.invertible())) {
      AUD_LOG_OBJ(INFO, &link) << "Skipping this capture source -- it isn't ready";
      continue;
    }

    // Update clock transformation if needed.
    UpdateTransformation(&mixer.bookkeeping(), rb_snap);

    // Based on current timestamp, determine which ring buffer portions can be safely read. This
    // safe area will be contiguous, although it may be split by the ring boundary. Determine the
    // starting PTS of these region(s), expressed in fractional source frames.
    //
    // TODO(13688): This mix job handling is similar to sections in AudioOutput that sample from
    // packet sources. Here we basically model the available ring buffer space as either 1 or 2
    // packets, depending on which regions can be safely read. Re-factor so both AudioCapturer and
    // AudioOutput can sample from packets and ring-buffers, sharing common logic across input mix
    // pump (AudioCapturer) and output mix pump (AudioOutput).
    //
    const auto& rb = rb_snap.ring_buffer;
    auto now = zx::clock::get_monotonic().get();

    int64_t end_fence_frames =
        (info.clock_mono_to_frac_source_frames.Apply(now)) >> kPtsFractionalBits;
    // If, because of significant FIFO depth or external delay, the calculated end_fence_frames
    // value is in the past, MOD it up into our ring buffer range.
    while (end_fence_frames < 0) {
      end_fence_frames += rb->frames();
    }

    auto start_fence_frames = end_fence_frames - rb_snap.end_fence_to_start_fence_frames;
    auto rb_frames = rb->frames();

    // Sometimes, because of audio input devices with large FIFO depth (or external delay),
    // start_fence_frames can be negative at stream-start time. If so, bring start_fence_frames to 0
    // and ensure that end_fence_frames is still within the ring range.
    FX_CHECK(end_fence_frames >= 0);

    start_fence_frames = std::max(start_fence_frames, 0l);
    FX_DCHECK(end_fence_frames - start_fence_frames < rb_frames);

    uint32_t start_frames_mod = start_fence_frames % rb_frames;
    uint32_t end_frames_mod = end_fence_frames % rb_frames;

    RbRegion regions[2];
    if (start_frames_mod <= end_frames_mod) {
      // One region
      regions[0].srb_pos = start_frames_mod;
      regions[0].len = end_frames_mod - start_frames_mod;
      regions[0].sfrac_pts = start_fence_frames << kPtsFractionalBits;

      regions[1].len = 0;
    } else {
      // Two regions
      regions[0].srb_pos = start_frames_mod;
      regions[0].len = rb_frames - start_frames_mod;
      regions[0].sfrac_pts = start_fence_frames << kPtsFractionalBits;

      regions[1].srb_pos = 0;
      regions[1].len = end_frames_mod;
      regions[1].sfrac_pts = regions[0].sfrac_pts + (regions[0].len << kPtsFractionalBits);
    }

    if constexpr (VERBOSE_TIMING_DEBUG) {
      DumpRbRegions(regions);
    }

    uint32_t frames_left = mix_frames;
    float* buf = mix_buf_.get();

    // Now for each of the possible regions, intersect with our job and mix.
    for (const auto& region : regions) {
      // If we encounter a region of zero length, we are done.
      if (region.len == 0) {
        break;
      }

      // Determine the first and last sampling points of this job, in fractional source frames.
      FX_DCHECK(frames_left > 0);
      const auto& trans = info.dest_frames_to_frac_source_frames;
      int64_t job_start = trans.Apply(frame_count_ + mix_frames - frames_left);
      int64_t job_end = job_start + trans.rate().Scale(frames_left - 1);

      // Determine the PTS of the final frame of audio in our source region.
      int64_t region_last_frame_pts = (region.sfrac_pts + ((region.len - 1) << kPtsFractionalBits));
      int64_t rb_last_frame_pts = (end_fence_frames - 1) << kPtsFractionalBits;
      FX_DCHECK(rb_last_frame_pts >= region.sfrac_pts);

      if constexpr (VERBOSE_TIMING_DEBUG) {
        auto job_start_cm = info.clock_mono_to_frac_source_frames.Inverse().Apply(job_start);
        auto job_end_cm = info.clock_mono_to_frac_source_frames.Inverse().Apply(job_end);
        auto region_start_cm =
            info.clock_mono_to_frac_source_frames.Inverse().Apply(region.sfrac_pts);
        auto region_end_cm =
            info.clock_mono_to_frac_source_frames.Inverse().Apply(rb_last_frame_pts);

        AUD_VLOG_OBJ(SPEW, this) << "Will mix " << job_start_cm << "-" << job_end_cm << " ("
                                 << std::hex << job_start << "-" << job_end << ")";
        AUD_VLOG_OBJ(SPEW, this) << "Region   " << region_start_cm << "-" << region_end_cm << " ("
                                 << std::hex << region.sfrac_pts << "-" << region_last_frame_pts
                                 << ")";
        AUD_VLOG_OBJ(SPEW, this) << "Buffer   " << region_start_cm << "-" << region_end_cm << " ("
                                 << std::hex << region.sfrac_pts << "-" << rb_last_frame_pts << ")";
      }

      // If this source region's final frame occurs before our filter's negative edge (centered at
      // this job's first sample), this source region is entirely in the past and must be skipped.
      // We have overflowed; we could have started [job_start-region_start+negative_edge] sooner.
      if (region_last_frame_pts < (job_start - mixer.neg_filter_width())) {
        if (rb_last_frame_pts < (job_start - mixer.neg_filter_width())) {
          auto clock_mono_late =
              zx::nsec(info.clock_mono_to_frac_source_frames.rate().Inverse().Scale(
                  job_start - rb_last_frame_pts));
          OverflowOccurred(rb_last_frame_pts, job_start, clock_mono_late);
        }
        // Move on to the next region
        continue;
      }
      // Otherwise, if this job_start is beyond this region, skip it (regardless of width)
      if (region.sfrac_pts + (region.len << kPtsFractionalBits) < job_start) {
        continue;
      }

      // If the PTS of the first frame of audio in our source region is after
      // the positive window edge of our filter centered at our job's sampling
      // point, then source region is entirely in the future and we are done.
      if (region.sfrac_pts > (job_end + mixer.pos_filter_width())) {
        break;
      }

      // Looks like this source region intersects our mix job (when including its filter). Compute
      // where in the intermediate buffer the first produced frame will be placed, as well as where,
      // relative to start of source region, the first sampling point will be.
      int64_t source_offset_64 = job_start - region.sfrac_pts;
      int64_t dest_offset_64 = 0;
      int64_t first_sample_pos_window_edge = job_start + mixer.pos_filter_width();

      const TimelineRate& dest_to_src = info.dest_frames_to_frac_source_frames.rate();
      // If source region's first frame is after filter's positive edge, skip some output frames.
      if (region.sfrac_pts > first_sample_pos_window_edge) {
        int64_t src_to_skip = region.sfrac_pts - first_sample_pos_window_edge;

        // In scaling our (fractional) source_offset to (integral) dest_offset, we want to "round
        // up" to the next integer dest frame, but the 'scale' operation truncates any fractional
        // result. ALL source_offset values have SOME fractional component except for the X.0 case.
        // So to "round up" while scaling, we subtract the smallest fractional value first, then
        // scale-truncate, then add one to the final result.
        dest_offset_64 = dest_to_src.Inverse().Scale(src_to_skip - 1) + 1;
        source_offset_64 += dest_to_src.Scale(dest_offset_64);

        PartialOverflowOccurred(source_offset_64, dest_offset_64);
      }

      FX_DCHECK(dest_offset_64 >= 0);
      FX_DCHECK(dest_offset_64 < static_cast<int64_t>(mix_frames));
      FX_DCHECK(source_offset_64 <= std::numeric_limits<int32_t>::max());
      FX_DCHECK(source_offset_64 >= std::numeric_limits<int32_t>::min());

      uint32_t region_frac_frame_len = region.len << kPtsFractionalBits;
      auto dest_offset = static_cast<uint32_t>(dest_offset_64);
      auto frac_source_offset = static_cast<int32_t>(source_offset_64);

      FX_DCHECK(frac_source_offset < static_cast<int32_t>(region_frac_frame_len))
          << std::hex << frac_source_offset << " must be less than "
          << static_cast<int32_t>(region_frac_frame_len);
      const uint8_t* region_source = rb->virt() + (region.srb_pos * rb->frame_size());

      // Invalidate the region of the cache we are about to read, on architectures requiring it.
      //
      // TODO(35022): Optimize this. In particular...
      // 1) When we have multiple clients of this ring buffer, only invalidate a section once.
      // 2) If this ring buffer is not fed directly from hardware, don't invalidate cache at all.
      //
      // Also, at some point double-check that mixer filter width is accounted for properly here.
      FX_DCHECK(dest_offset <= frames_left);
      uint64_t cache_target_frac_frames = dest_to_src.Scale(frames_left - dest_offset);
      uint32_t cache_target_frames = ((cache_target_frac_frames - 1) >> kPtsFractionalBits) + 1;
      cache_target_frames = std::min(cache_target_frames, region.len);
      zx_cache_flush(region_source, cache_target_frames * rb->frame_size(),
                     ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);

      // Looks like we are ready to go. Mix.
      // TODO(13415): integrate bookkeeping into the Mixer itself.
      //
      // When calling Mix(), we communicate the resampling rate with three
      // parameters. We augment frac_step_size with rate_modulo and denominator
      // arguments that capture the remaining rate component that cannot be
      // expressed by a 19.13 fixed-point step_size. Note: frac_step_size and
      // frac_source_offset use the same format -- they have the same limitations
      // in what they can and cannot communicate. This begs two questions:
      //
      // Q1: For perfect position accuracy, just as we track incoming/outgoing
      // fractional source offset, wouldn't we also need a src_pos_modulo?
      // A1: Yes, for optimum position accuracy (within quantization limits), we
      // SHOULD incorporate the ongoing subframe_position_modulo in this way.
      //
      // For now, we are deferring this work, tracking it with MTWN-128.
      //
      // Q2: Why did we solve this issue for rate but not for initial position?
      // A2: We solved this issue for *rate* because its effect accumulates over
      // time, causing clearly measurable distortion that becomes crippling with
      // larger jobs. For *position*, there is no accumulated magnification over
      // time -- in analyzing the distortion that this should cause, mix job
      // size would affect the distortion frequency but not amplitude. We expect
      // the effects to be below audible thresholds. Until the effects are
      // measurable and attributable to this jitter, we will defer this work.
      //
      // Update: src_pos_modulo is added to Mix(), but for now we omit it here.
      bool consumed_source = mixer.Mix(buf, frames_left, &dest_offset, region_source,
                                       region_frac_frame_len, &frac_source_offset, accumulate);
      FX_DCHECK(dest_offset <= frames_left);

      if (!consumed_source) {
        // Looks like we didn't consume all of this region. Assert that we
        // have produced all of our frames and we are done.
        FX_DCHECK(dest_offset == frames_left);
        break;
      }

      buf += dest_offset * format_->channels;
      frames_left -= dest_offset;
      if (!frames_left) {
        break;
      }
    }

    // We have now added something to the intermediate mix buffer. For our next
    // source to process, we cannot assume that it is just silence. Set the
    // accumulate flag to tell the mixer to accumulate (not overwrite).
    accumulate = true;
  }

  return true;
}

void AudioCapturerImpl::UpdateTransformation(Mixer::Bookkeeping* info,
                                             const AudioDriver::RingBufferSnapshot& rb_snap) {
  TRACE_DURATION("audio", "AudioCapturerImpl::UpdateTransformation");
  FX_DCHECK(info != nullptr);

  if ((info->dest_trans_gen_id == dest_frames_to_clock_mono_gen_.get()) &&
      (info->source_trans_gen_id == rb_snap.gen_id)) {
    return;
  }

  FX_DCHECK(rb_snap.ring_buffer != nullptr);
  FX_DCHECK(rb_snap.ring_buffer->frame_size() != 0);
  FX_DCHECK(rb_snap.clock_mono_to_ring_pos_bytes.invertible());

  TimelineRate src_bytes_to_frac_frames(1u << kPtsFractionalBits,
                                        rb_snap.ring_buffer->frame_size());

  // This represents ring-buffer frac frames since DMA engine was started
  auto clock_mono_to_ring_pos_frac_frames = TimelineFunction::Compose(
      TimelineFunction(src_bytes_to_frac_frames), rb_snap.clock_mono_to_ring_pos_bytes);

  info->dest_frames_to_frac_source_frames =
      TimelineFunction::Compose(clock_mono_to_ring_pos_frac_frames, dest_frames_to_clock_mono_);

  // Our frac source frame sampling point should lag the ring buffer DMA position by this offset.
  auto offset = static_cast<int64_t>(rb_snap.position_to_end_fence_frames) << kPtsFractionalBits;
  info->clock_mono_to_frac_source_frames = TimelineFunction::Compose(
      TimelineFunction(-offset, 0, TimelineRate(1u, 1u)), clock_mono_to_ring_pos_frac_frames);

  int64_t tmp_step_size = info->dest_frames_to_frac_source_frames.rate().Scale(1);
  FX_DCHECK(tmp_step_size >= 0);
  FX_DCHECK(tmp_step_size <= std::numeric_limits<uint32_t>::max());
  info->step_size = static_cast<uint32_t>(tmp_step_size);
  info->denominator = info->SnapshotDenominatorFromDestTrans();
  info->rate_modulo = info->dest_frames_to_frac_source_frames.rate().subject_delta() -
                      (info->denominator * info->step_size);

  FX_DCHECK(info->denominator > 0);
  info->dest_trans_gen_id = dest_frames_to_clock_mono_gen_.get();
  info->source_trans_gen_id = rb_snap.gen_id;
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
  dest_frames_to_clock_mono_ = TimelineFunction();
  dest_frames_to_clock_mono_gen_.Next();

  // If we had a timer set, make sure that it is canceled. There is no point in
  // having it armed right now as we are in the process of stopping.
  mix_timer_.Cancel();

  // Transition to the AsyncStoppingCallbackPending state, and signal the
  // service thread so it can complete the stop operation.
  state_.store(State::AsyncStoppingCallbackPending);
  async::PostTask(threading_model_.FidlDomain().dispatcher(),
                  [thiz = fbl::RefPtr(this)]() { thiz->FinishAsyncStopThunk(); });
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
  auto pending_capture_buffer =
      PcbAllocator::New(async_next_frame_offset_, async_frames_per_packet_, nullptr);
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
  async::PostTask(threading_model_.FidlDomain().dispatcher(), [self_ref = fbl::RefPtr(this)]() {
    self_ref->route_graph_.RemoveCapturer(self_ref.get());
  });
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
    pkt.payload_offset = finished_buffer.offset_frames * bytes_per_frame_;
    pkt.payload_size = finished_buffer.filled_frames * bytes_per_frame_;

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

void AudioCapturerImpl::UpdateFormat(fuchsia::media::AudioSampleFormat sample_format,
                                     uint32_t channels, uint32_t frames_per_second) {
  TRACE_DURATION("audio", "AudioCapturerImpl::UpdateFormat");
  // Record our new format.
  FX_DCHECK(state_.load() == State::WaitingForVmo);
  format_->sample_format = sample_format;
  format_->channels = channels;
  format_->frames_per_second = frames_per_second;
  bytes_per_frame_ = channels * BytesPerSample(sample_format);

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
  dest_frames_to_clock_mono_rate_ = TimelineRate(ZX_SEC(1), format_->frames_per_second);
  tmp = dest_frames_to_clock_mono_rate_.Inverse().Scale(kMaxTimePerCapture);
  max_frames_per_capture_ = static_cast<uint32_t>(tmp);

  FX_DCHECK(tmp <= std::numeric_limits<uint32_t>::max());
  FX_DCHECK(max_frames_per_capture_ > 0);
}

zx_status_t AudioCapturerImpl::ChooseMixer(const fbl::RefPtr<AudioLink>& link) {
  TRACE_DURATION("audio", "AudioCapturerImpl::ChooseMixer");
  FX_DCHECK(link != nullptr);

  const auto& source = link->GetSource();
  FX_DCHECK(source);

  if (!source->is_input() && !source->is_output()) {
    FX_LOGS(ERROR) << "Failed to find mixer for source of type "
                   << static_cast<uint32_t>(source->type());
    return ZX_ERR_INVALID_ARGS;
  }

  // Throttle outputs are the only driver-less devices. MTWN-52 is the work to
  // remove this construct and have packet sources maintain pending packet
  // queues, trimmed by a thread from the pool managed by the device manager.
  auto& device = static_cast<AudioDevice&>(*source);
  if (device.driver() == nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  // Get the driver's current format. Without one, we can't setup the mixer.
  fuchsia::media::AudioStreamTypePtr source_format;
  source_format = device.driver()->GetSourceFormat();
  if (!source_format) {
    FX_LOGS(WARNING) << "Failed to find mixer. Source currently has no configured format";
    return ZX_ERR_BAD_STATE;
  }

  // Select a mixer.
  auto mixer = Mixer::Select(*source_format, *format_);
  if (!mixer) {
    FX_LOGS(WARNING) << "Failed to find mixer for capturer.";
    FX_LOGS(WARNING) << "Source cfg: rate " << source_format->frames_per_second << " ch "
                     << source_format->channels << " sample fmt "
                     << fidl::ToUnderlying(source_format->sample_format);
    FX_LOGS(WARNING) << "Dest cfg  : rate " << format_->frames_per_second << " ch "
                     << format_->channels << " sample fmt "
                     << fidl::ToUnderlying(format_->sample_format);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // The Gain object contains multiple stages. In capture, device (or
  // master) gain is "source" gain and stream gain is "dest" gain.
  //
  // First, set the source gain -- based on device gain.
  if (device.is_input()) {
    // Initialize the source gain, from (Audio Input) device settings.
    fuchsia::media::AudioDeviceInfo device_info;
    device.GetDeviceInfo(&device_info);

    const auto muted = device_info.gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute;
    mixer->bookkeeping().gain.SetSourceGain(
        muted ? fuchsia::media::audio::MUTED_GAIN_DB
              : std::clamp(device_info.gain_info.gain_db, Gain::kMinGainDb, Gain::kMaxGainDb));
  }

  // Else (if device is an Audio Output), use default SourceGain (Unity). Device
  // gain has already been applied "on the way down" during the render mix.
  link->set_mixer(std::move(mixer));
  return ZX_OK;
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
    RemoveFromRouteGraph();
    return;
  }

  // If the incoming SetGain request represents no change, we're done
  // (once we add gain ramping, this type of check isn't workable).
  if (stream_gain_db_ == gain_db) {
    return;
  }

  REP(SettingCapturerGain(*this, gain_db));

  stream_gain_db_.store(gain_db);
  volume_manager_.NotifyStreamChanged(this);

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

  volume_manager_.NotifyStreamChanged(this);
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
