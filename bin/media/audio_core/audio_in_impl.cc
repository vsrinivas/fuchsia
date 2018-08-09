// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/audio_in_impl.h"

#include <fbl/auto_call.h>

#include "garnet/bin/media/audio_core/audio_core_impl.h"
#include "lib/fxl/logging.h"
#include "lib/media/audio/types.h"

// Allow up to (at most) 256 slabs of pending capture buffers.  At 16KB per
// slab, this means we will deny allocations after 4MB.  If we ever need more
// than 4MB of pending capture buffer bookkeeping, something has gone seriously
// wrong.
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(
    ::media::audio::AudioInImpl::PcbAllocatorTraits, 0x100);

namespace media {
namespace audio {

zx_duration_t kAssumedWorstSourceFenceTime = ZX_MSEC(5);

constexpr float kInitialCaptureGain = 0.0;

// static
AtomicGenerationId AudioInImpl::PendingCaptureBuffer::sequence_generator;

fbl::RefPtr<AudioInImpl> AudioInImpl::Create(
    fidl::InterfaceRequest<fuchsia::media::AudioIn> audio_in_request,
    AudioCoreImpl* owner, bool loopback) {
  return fbl::AdoptRef(
      new AudioInImpl(std::move(audio_in_request), owner, loopback));
}

AudioInImpl::AudioInImpl(
    fidl::InterfaceRequest<fuchsia::media::AudioIn> audio_in_request,
    AudioCoreImpl* owner, bool loopback)
    : AudioObject(Type::AudioIn),
      binding_(this, std::move(audio_in_request)),
      owner_(owner),
      state_(State::WaitingForVmo),
      loopback_(loopback),
      db_gain_(kInitialCaptureGain) {
  // TODO(johngro) : See MG-940.  Eliminate this priority boost as soon as we
  // have a more official way of meeting real-time latency requirements.
  mix_domain_ = ::dispatcher::ExecutionDomain::Create(24);
  mix_wakeup_ = ::dispatcher::WakeupEvent::Create();
  mix_timer_ = ::dispatcher::Timer::Create();

  binding_.set_error_handler([this]() { Shutdown(); });
  source_link_refs_.reserve(16u);

  // TODO(johngro) : Initialize this with the native configuration of the source
  // we are initally bound to.
  format_ = fuchsia::media::AudioStreamType::New();
  UpdateFormat(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 8000);
}

AudioInImpl::~AudioInImpl() {
  // TODO(johngro) : ASSERT that the execution domain has shut down.
  FXL_DCHECK(!payload_buf_vmo_.is_valid());
  FXL_DCHECK(payload_buf_virt_ == nullptr);
  FXL_DCHECK(payload_buf_size_ == 0);
}

void AudioInImpl::SetInitialFormat(fuchsia::media::AudioStreamType format) {
  UpdateFormat(format.sample_format, format.channels, format.frames_per_second);
}

void AudioInImpl::Shutdown() {
  // Disconnect from everything we were connected to.
  PreventNewLinks();
  Unlink();

  // Close any client connections.
  if (binding_.is_bound()) {
    binding_.set_error_handler(nullptr);
    binding_.Unbind();
  }

  // Deactivate our mixing domain and synchronize with any in-flight operations.
  mix_domain_->Deactivate();

  // Release our buffer resources.
  //
  // TODO(johngro): Change this to use the DriverRingBuffer utility class (and
  // perhaps rename the DriverRingBuffer class to something more generic, like
  // RingBufferHelper or something, since this would be a use which is not
  // driver specific).
  if (payload_buf_virt_ != nullptr) {
    FXL_DCHECK(payload_buf_size_ != 0);
    zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(payload_buf_virt_),
                                 payload_buf_size_);
    payload_buf_virt_ = nullptr;
    payload_buf_size_ = 0;
    payload_buf_frames_ = 0;
  }

  payload_buf_vmo_.reset();

  // Make sure we have left the set of active audio ins.
  if (InContainer()) {
    owner_->GetDeviceManager().RemoveAudioIn(this);
  }

  state_.store(State::Shutdown);
}

zx_status_t AudioInImpl::InitializeSourceLink(const AudioLinkPtr& link) {
  zx_status_t res;

  // Allocate our bookkeeping for our link.
  std::unique_ptr<CaptureLinkBookkeeping> bk(new CaptureLinkBookkeeping());
  link->set_bookkeeping(std::move(bk));

  // Choose a mixer
  switch (state_.load()) {
    // If we have not received a VMO yet, then we are still waiting for the user
    // to commit to a format.  We cannot select a mixer yet.
    case State::WaitingForVmo:
      res = ZX_OK;
      break;

    // We are operational.  Go ahead and choose a mixer.
    case State::OperatingSync:
    case State::OperatingAsync:
    case State::AsyncStopping:
    case State::AsyncStoppingCallbackPending:
      res = ChooseMixer(link);
      break;

    // If we are shut down, then I'm not sure why new links are being added, but
    // just go ahead and reject this one.  We will be going away shortly.
    case State::Shutdown:
      res = ZX_ERR_BAD_STATE;
      break;
  }

  return res;
}

void AudioInImpl::GetStreamType(GetStreamTypeCallback cbk) {
  fuchsia::media::StreamType ret;
  ret.encoding = fuchsia::media::AUDIO_ENCODING_LPCM;
  ret.medium_specific.set_audio(*format_);
  cbk(std::move(ret));
}

void AudioInImpl::SetPcmStreamType(
    fuchsia::media::AudioStreamType stream_type) {
  // If something goes wrong, hang up the phone and shutdown.
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });

  // If our shared buffer has already been assigned, then we are operating and
  // the mode can no longer be changed.
  State state = state_.load();
  if (state != State::WaitingForVmo) {
    FXL_DCHECK(payload_buf_vmo_.is_valid());
    FXL_LOG(ERROR) << "Cannot change capture mode while operating!"
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  // Sanity check the details of the mode request.
  if ((stream_type.channels < fuchsia::media::MIN_PCM_CHANNEL_COUNT) ||
      (stream_type.channels > fuchsia::media::MAX_PCM_CHANNEL_COUNT)) {
    FXL_LOG(ERROR) << "Bad channel count, " << stream_type.channels
                   << " is not in the range ["
                   << fuchsia::media::MIN_PCM_CHANNEL_COUNT << ", "
                   << fuchsia::media::MAX_PCM_CHANNEL_COUNT << "]";
    return;
  }

  if ((stream_type.frames_per_second <
       fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) ||
      (stream_type.frames_per_second >
       fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)) {
    FXL_LOG(ERROR) << "Bad frame rate, " << stream_type.frames_per_second
                   << " is not in the range ["
                   << fuchsia::media::MIN_PCM_FRAMES_PER_SECOND << ", "
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
      FXL_LOG(ERROR) << "Bad sample format "
                     << fidl::ToUnderlying(stream_type.sample_format);
      return;
  }

  // Success, record our new format.
  UpdateFormat(stream_type.sample_format, stream_type.channels,
               stream_type.frames_per_second);

  cleanup.cancel();
}

void AudioInImpl::AddPayloadBuffer(uint32_t id, zx::vmo payload_buf_vmo) {
  if (id != 0) {
    FXL_LOG(ERROR) << "Only buffer ID 0 is currently supported.";
    Shutdown();
    return;
  }

  FXL_DCHECK(payload_buf_vmo.is_valid());

  // If something goes wrong, hang up the phone and shutdown.
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });
  zx_status_t res;

  State state = state_.load();
  if (state != State::WaitingForVmo) {
    FXL_DCHECK(payload_buf_vmo_.is_valid());
    FXL_DCHECK(payload_buf_virt_ != nullptr);
    FXL_DCHECK(payload_buf_size_ != 0);
    FXL_DCHECK(payload_buf_frames_ != 0);
    FXL_LOG(ERROR) << "Bad state while assigning payload buffer "
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  } else {
    FXL_DCHECK(payload_buf_virt_ == nullptr);
    FXL_DCHECK(payload_buf_size_ == 0);
    FXL_DCHECK(payload_buf_frames_ == 0);
  }

  // Take ownership of the VMO, fetch and sanity check the size.
  payload_buf_vmo_ = std::move(payload_buf_vmo);
  res = payload_buf_vmo_.get_size(&payload_buf_size_);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to fetch payload buffer VMO size (res = " << res
                   << ")";
    return;
  }

  FXL_CHECK(bytes_per_frame_ > 0);
  constexpr uint64_t max_uint32 = std::numeric_limits<uint32_t>::max();
  if ((payload_buf_size_ < bytes_per_frame_) ||
      (payload_buf_size_ > (max_uint32 * bytes_per_frame_))) {
    FXL_LOG(ERROR) << "Bad payload buffer VMO size (size = "
                   << payload_buf_size_
                   << ", bytes per frame = " << bytes_per_frame_ << ")";
    return;
  }

  payload_buf_frames_ =
      static_cast<uint32_t>(payload_buf_size_ / bytes_per_frame_);

  // Allocate our intermediate buffer for mixing.
  //
  // TODO(johngro):  This does not need to be as long (in frames) as the user
  // supplied VMO.  Limit this to something more reasonable.
  mix_buf_.reset(new float[payload_buf_frames_]);

  // Map the VMO into our process.
  uintptr_t tmp;
  res = zx::vmar::root_self()->map(0, payload_buf_vmo_, 0, payload_buf_size_,
                                   ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                   &tmp);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map payload buffer VMO (res = " << res << ")";
    return;
  }

  payload_buf_virt_ = reinterpret_cast<void*>(tmp);

  // Activate the dispatcher primitives we will use to drive the mixing process.
  res = mix_wakeup_->Activate(
      mix_domain_, [this](::dispatcher::WakeupEvent* event) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, mix_domain_);
        FXL_DCHECK(event == mix_wakeup_.get());
        return Process();
      });

  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed activate wakeup event (res = " << res << ")";
    return;
  }

  res = mix_timer_->Activate(
      mix_domain_, [this](::dispatcher::Timer* timer) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, mix_domain_);
        FXL_DCHECK(timer == mix_timer_.get());
        return Process();
      });

  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed activate timer (res = " << res << ")";
    return;
  }

  // Next, select out output formatter.
  output_formatter_ = OutputFormatter::Select(format_);
  if (output_formatter_ == nullptr) {
    FXL_LOG(ERROR) << "Failed to select output formatter";
    return;
  }

  // Things went well.  While we may fail to create links to audio sources from
  // this point forward, we have successfully configured the mode for this
  // audio in, so we are now in the OperatingSync state.
  state_.store(State::OperatingSync);

  // Let our source links know about the format that we prefer.
  //
  // TODO(johngro): Remove this.  Audio sources do not care what we prefer to
  // capture.  If an AudioInput is going to be reconfigured because of our
  // needs, it will happen at the policy level before we get linked up.
  {
    fbl::AutoLock links_lock(&links_lock_);
    for (auto& link : source_links_) {
      const auto& source = link->GetSource();
      switch (source->type()) {
        case AudioObject::Type::Output:
        case AudioObject::Type::Input: {
          auto device = static_cast<AudioDevice*>(source.get());
          device->NotifyDestFormatPreference(format_);
          break;
        }

        case AudioObject::Type::AudioOut:
          // TODO(johngro): Support capturing from packet sources
          break;

        case AudioObject::Type::AudioIn:
          FXL_DCHECK(false);
          break;
      }
    }
  }

  // Select a mixer for each active link here.
  //
  // TODO(johngro): We should probably just stop doing this here.  It would be
  // best if had an invariant which said that source and destination objects
  // could not be linked unless both had a configured format.  Dynamic changes
  // of format would require breaking and reforming links in this case, which
  // would make it difficult to ever do a seamless format change (something
  // which already would be rather difficult to do).
  std::vector<std::shared_ptr<AudioLink>> cleanup_list;
  {
    fbl::AutoLock links_lock(&links_lock_);
    for (const auto& link : source_links_) {
      if (ChooseMixer(link) != ZX_OK) {
        cleanup_list.push_back(link);
      }
    }
  }

  for (const auto& link : cleanup_list) {
    AudioObject::RemoveLink(link);
  }

  cleanup.cancel();
}

void AudioInImpl::RemovePayloadBuffer(uint32_t id) {
  FXL_LOG(ERROR) << "RemovePayloadBuffer is not currently supported.";
  Shutdown();
}

void AudioInImpl::CaptureAt(uint32_t payload_buffer_id, uint32_t offset_frames,
                            uint32_t num_frames, CaptureAtCallback cbk) {
  if (payload_buffer_id != 0) {
    FXL_LOG(ERROR) << "payload_buffer_id must be 0 for now.";
    return;
  }

  // If something goes wrong, hang up the phone and shutdown.
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });

  // It is illegal to call CaptureAt unless we are currently operating in
  // synchronous mode.
  State state = state_.load();
  if (state != State::OperatingSync) {
    FXL_LOG(ERROR) << "CaptureAt called while not operating in sync mode "
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  // Buffers submitted by clients must exist entirely within the shared payload
  // buffer, and must have at least some payloads in them.
  uint64_t buffer_end = static_cast<uint64_t>(offset_frames) + num_frames;
  if (!num_frames || (buffer_end > payload_buf_frames_)) {
    FXL_LOG(ERROR) << "Bad buffer range submitted. "
                   << " offset " << offset_frames << " length " << num_frames
                   << ".  Shared buffer is " << payload_buf_frames_
                   << " frames long.";
    return;
  }

  // Allocate bookkeeping to track this pending capture operation.
  auto pending_capture_buffer =
      PcbAllocator::New(offset_frames, num_frames, std::move(cbk));
  if (pending_capture_buffer == nullptr) {
    FXL_LOG(ERROR) << "Failed to allocate pending capture buffer!";
    return;
  }

  // Place the capture operation on the pending list.
  bool wake_mixer;
  {
    fbl::AutoLock pending_lock(&pending_lock_);
    wake_mixer = pending_capture_buffers_.is_empty();
    pending_capture_buffers_.push_back(std::move(pending_capture_buffer));
  }

  // If the pending list was empty, we need to poke the mixer.
  if (wake_mixer) {
    mix_wakeup_->Signal();
  }

  // Things went well.  Cancel the cleanup timer and we are done.
  cleanup.cancel();
}

void AudioInImpl::ReleasePacket(fuchsia::media::StreamPacket packet) {
  // TODO(mpuryear): Implement.
  FXL_LOG(ERROR) << "ReleasePacket not implemented yet.";
  Shutdown();
}

void AudioInImpl::DiscardAllPacketsNoReply() {
  // It is illegal to call Flush unless we are currently operating in
  // synchronous mode.
  State state = state_.load();
  if (state != State::OperatingSync) {
    FXL_LOG(ERROR) << "Flush called while not operating in sync mode "
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    Shutdown();
    return;
  }

  // Lock and move the contents of the finished list and pending list to a
  // temporary list.  Then deliver the flushed buffers back to the client and
  // send an OnEndOfStream event.
  //
  // Note: It is possible that the capture thread is currently mixing frames for
  // the buffer at the head of the pending queue at the time that we clear the
  // queue.  The fact that these frames were mixed will not be reported to the
  // client, however the frames will be written to the shared payload buffer.
  PcbList finished;
  {
    fbl::AutoLock pending_lock(&pending_lock_);
    finished = std::move(finished_capture_buffers_);
    finished.splice(finished.end(), pending_capture_buffers_);
  }

  if (!finished.is_empty()) {
    FinishBuffers(finished);
    binding_.events().OnEndOfStream();
  }
}

void AudioInImpl::DiscardAllPackets(DiscardAllPacketsCallback cbk) {
  DiscardAllPacketsNoReply();
  if (binding_.is_bound()) {
    cbk();
  }
}

void AudioInImpl::StartAsyncCapture(uint32_t frames_per_packet) {
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });

  // In order to enter async mode, we must be operating in synchronous mode, and
  // we must not have any pending buffers in flight.
  State state = state_.load();
  if (state != State::OperatingSync) {
    FXL_LOG(ERROR) << "Bad state while attempting to enter async capture mode "
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  bool queues_empty;
  {
    fbl::AutoLock pending_lock(&pending_lock_);
    queues_empty = pending_capture_buffers_.is_empty() &&
                   finished_capture_buffers_.is_empty();
  }

  if (!queues_empty) {
    FXL_LOG(ERROR) << "Attempted to enter async capture mode with capture "
                      "buffers still in flight.";
    return;
  }

  // Sanity check the number of frames per packet the user is asking for.
  //
  // TODO(johngro) : This effectivly sets the minimum number of frames per
  // packet to produce at 1.  This is still absurdly low; what is the proper
  // number?  We should decide on a proper lower bound, document it, and enforce
  // the limit here.
  if (frames_per_packet == 0) {
    FXL_LOG(ERROR) << "Frames per packet may not be zero.";
    return;
  }

  FXL_DCHECK(payload_buf_frames_ > 0);
  if (frames_per_packet > (payload_buf_frames_ / 2)) {
    FXL_LOG(ERROR) << "There must be enough room in the shared payload buffer ("
                   << payload_buf_frames_
                   << " frames) to fit at least two packets of the requested "
                      "number of frames per packet ("
                   << frames_per_packet << " frames).";
    return;
  }

  // Everything looks good...
  // 1) Record the number of frames per packet we want to produce
  // 2) Transition to the OperatingAsync state
  // 3) Kick the work thread to get the ball rolling.
  async_frames_per_packet_ = frames_per_packet;
  state_.store(State::OperatingAsync);
  mix_wakeup_->Signal();
  cleanup.cancel();
}

void AudioInImpl::StopAsyncCaptureNoReply() { StopAsyncCapture(nullptr); }

void AudioInImpl::StopAsyncCapture(StopAsyncCaptureCallback cbk) {
  // In order to leave async mode, we must be operating in async mode, or we
  // must already be operating in sync mode (in which case, there is really
  // nothing to do but signal the callback if one was provided)
  State state = state_.load();
  if (state == State::OperatingSync) {
    if (cbk != nullptr) {
      cbk();
    }
    return;
  }

  if (state != State::OperatingAsync) {
    FXL_LOG(ERROR) << "Bad state while attempting to stop async capture mode "
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    Shutdown();
    return;
  }

  // Stash our callback, transition to the AsyncStopping state, then poke the
  // work thread so it knows that it needs to shut down.
  FXL_DCHECK(pending_async_stop_cbk_ == nullptr);
  pending_async_stop_cbk_ = std::move(cbk);
  state_.store(State::AsyncStopping);
  mix_wakeup_->Signal();
}

zx_status_t AudioInImpl::Process() {
  while (true) {
    // Start by figure out what state we are currently in for this cycle.
    bool async_mode = false;
    switch (state_.load()) {
      // If we are still waiting for a VMO, we should not be operating right
      // now.
      case State::WaitingForVmo:
        FXL_DCHECK(false);
        ShutdownFromMixDomain();
        return ZX_ERR_INTERNAL;

      // If we have woken up while we are in the callback pending state, this is
      // a spurious wakeup.  Just ignore it.
      case State::AsyncStoppingCallbackPending:
        return ZX_OK;

      // If we were operating in async mode, but we have been asked to stop, do
      // so now.
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
        // This should be impossible.  If the main message loop thread shut us
        // down, then it should have shut down our execution domain and waited
        // for any in flight tasks to complete before setting the state_
        // variable to Shutdown.  If we shut ourselves down, we should have shut
        // down the execution domain and the immediately exited from the
        // handler.
        FXL_CHECK(false);
        return ZX_ERR_INTERNAL;
    }

    // Look at the front of the queue and figure out the position in the payload
    // buffer we are supposed to be filling and get to work.
    void* mix_target = nullptr;
    uint32_t mix_frames;
    uint32_t buffer_sequence_number;
    {
      fbl::AutoLock pending_lock(&pending_lock_);
      if (!pending_capture_buffers_.is_empty()) {
        auto& p = pending_capture_buffers_.front();

        // This should have been established by CaptureAt; it had better still
        // be true.
        FXL_DCHECK((static_cast<uint64_t>(p.offset_frames) + p.num_frames) <=
                   payload_buf_frames_);
        FXL_DCHECK(p.filled_frames < p.num_frames);

        // If we don't know our timeline transformation, then the next buffer we
        // produce is guaranteed to be discontinuous relative to the previous
        // one (if any).
        if (!frames_to_clock_mono_.invertable()) {
          p.flags |= fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY;
        }

        // If we are still running, there should be no way that our shared
        // buffer has been stolen out from under us.
        FXL_DCHECK(payload_buf_virt_ != nullptr);

        uint64_t offset_bytes =
            bytes_per_frame_ *
            static_cast<uint64_t>(p.offset_frames + p.filled_frames);

        mix_target = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(payload_buf_virt_) + offset_bytes);
        mix_frames = p.num_frames - p.filled_frames;
        buffer_sequence_number = p.sequence_number;
      }
    }

    // If there was nothing in our pending capture buffer queue, then one of two
    // things is true.
    //
    // 1) We are operating in synchronous mode and our user is not supplying
    //    buffers fast enough.
    // 2) We are starting up in asynchronous mode and have not queued our first
    //    buffer yet.
    //
    // Either way, invalidate the frames_to_clock_mono transformation and make
    // sure we don't have a wakeup timer pending.  Then, if we are in
    // synchronous mode, simply get out.  If we are in asynchronous mode, reset
    // our async ring buffer state, add a new pending capture buffer to the
    // queue, and restart the main Process loop.
    if (mix_target == nullptr) {
      frames_to_clock_mono_ = TimelineFunction();
      frames_to_clock_mono_gen_.Next();
      frame_count_ = 0;
      mix_timer_->Cancel();

      if (!async_mode) {
        return ZX_OK;
      }

      // If we cannot queue a new pending buffer, it is a fatal error.  Simply
      // return instead of trying again as we are now shutting down.
      async_next_frame_offset_ = 0;
      if (!QueueNextAsyncPendingBuffer()) {
        // If this fails, QueueNextAsyncPendingBuffer should have already shut
        // us down.  Assert this.
        FXL_DCHECK(state_.load() == State::Shutdown);
        return ZX_ERR_INTERNAL;
      }
      continue;
    }

    // If we have yet to establish a timeline transformation from capture frames
    // to clock monotonic, establish one now.
    //
    // TODO(johngro) : If we have only one capture source, and our frame rate
    // matches their frame rate, align our start time exactly with one of their
    // sample boundaries.
    int64_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
    if (!frames_to_clock_mono_.invertable()) {
      // TODO(johngro) : It would be nice if we could alter the offsets in a
      // timeline function without needing to change the scale factor.  This
      // would allow us to establish a new mapping here without needing to
      // re-reduce the ratio between frames_per_second_ and nanoseconds every
      // time.  Since the frame rate we supply is already reduced, this step
      // should go pretty quickly.
      frames_to_clock_mono_ =
          TimelineFunction(now, frame_count_, frames_to_clock_mono_rate_);
      frames_to_clock_mono_gen_.Next();
      FXL_DCHECK(frames_to_clock_mono_.invertable());
    }

    // Limit our job size to our max job size.
    if (mix_frames > max_frames_per_capture_) {
      mix_frames = max_frames_per_capture_;
    }

    // Now figure out what time it will be when we can finish this job If this
    // time is in the future, wait until then.
    int64_t last_frame_time =
        frames_to_clock_mono_.Apply(frame_count_ + mix_frames);
    if (last_frame_time == TimelineRate::kOverflow) {
      FXL_LOG(ERROR)
          << "Fatal timeline overflow in capture mixer, shutting down capture.";
      ShutdownFromMixDomain();
      return ZX_ERR_INTERNAL;
    }

    if (last_frame_time > now) {
      // TODO(johngro) : Fix this.  We should not assume anything about the
      // fence times for our sources.  Instead, we should pay attention to what
      // the fence times are, and to the comings and goings of sources, and
      // update this number dynamically.
      //
      // Additionally, we need to be a bit careful when new sources show up.  If
      // a new source shows up and pushes the largest fence time out, the next
      // time we wake up, it will be early.  We will need to recognize this
      // condition and go back to sleep for a little bit before actually mixing.
      mix_timer_->Arm(last_frame_time + kAssumedWorstSourceFenceTime);
      return ZX_OK;
    }

    // Mix the requested number of frames from our sources to our intermediate
    // buffer, then the intermediate buffer into our output target.
    if (!MixToIntermediate(mix_frames)) {
      ShutdownFromMixDomain();
      return ZX_ERR_INTERNAL;
    }

    FXL_DCHECK(output_formatter_ != nullptr);
    output_formatter_->ProduceOutput(mix_buf_.get(), mix_target, mix_frames);

    // Update the pending buffer in progress, and if it is finished, send it
    // back to the user.  If the buffer has been flushed (there is either no
    // packet in the pending queue, or the front of the queue has a different
    // sequence number from the buffer we were working on), just move on.
    bool buffer_finished = false;
    bool wakeup_service_thread = false;
    {
      fbl::AutoLock pending_lock(&pending_lock_);
      if (!pending_capture_buffers_.is_empty()) {
        auto& p = pending_capture_buffers_.front();
        if (buffer_sequence_number == p.sequence_number) {
          // Update the filled status of the buffer.
          p.filled_frames += mix_frames;
          FXL_DCHECK(p.filled_frames <= p.num_frames);

          // Assign a timestamp if one has not already been assigned.
          if (p.capture_timestamp == fuchsia::media::NO_TIMESTAMP) {
            FXL_DCHECK(frames_to_clock_mono_.invertable());
            p.capture_timestamp = frames_to_clock_mono_.Apply(frame_count_);
          }

          // If we have finished filling this buffer, place it in the finished
          // queue to be sent back to the user.
          buffer_finished = p.filled_frames >= p.num_frames;
          if (buffer_finished) {
            wakeup_service_thread = finished_capture_buffers_.is_empty();
            finished_capture_buffers_.push_back(
                pending_capture_buffers_.pop_front());
          }
        } else {
          // It looks like we were flushed while we were mixing.  Invalidate our
          // timeline function, we will re-establish it and flag a discontinuity
          // next time we have work to do.
          frames_to_clock_mono_ =
              TimelineFunction(now, frame_count_, frames_to_clock_mono_rate_);
          frames_to_clock_mono_gen_.Next();
        }
      }
    }

    // Update the total number of frames we have mixed so far.
    frame_count_ += mix_frames;

    // If we need to poke the service thread, do so.
    if (wakeup_service_thread) {
      owner_->ScheduleMainThreadTask(
          [thiz = fbl::WrapRefPtr(this)]() { thiz->FinishBuffersThunk(); });
    }

    // If we are in async mode, and we just finished a buffer, queue a new
    // pending buffer (or die trying).
    if (buffer_finished && async_mode && !QueueNextAsyncPendingBuffer()) {
      // If this fails, QueueNextAsyncPendingBuffer should have already shut
      // us down.  Assert this.
      FXL_DCHECK(state_.load() == State::Shutdown);
      return ZX_ERR_INTERNAL;
    }
  }  // while (true)
}

void AudioInImpl::BindGainControl(
    fidl::InterfaceRequest<fuchsia::media::GainControl> request) {
  gain_control_bindings_.AddBinding(this, std::move(request));
}

void AudioInImpl::SetGain(float gain_db) {
  if ((gain_db < fuchsia::media::MUTED_GAIN) ||
      (gain_db > fuchsia::media::MAX_GAIN)) {
    FXL_LOG(ERROR) << "Invalid Gain " << gain_db;
    Shutdown();
    return;
  }

  db_gain_.store(gain_db);
}

void AudioInImpl::SetMute(bool muted) {
  // TODO(mpuryear): Implement.
}

bool AudioInImpl::MixToIntermediate(uint32_t mix_frames) {
  // Take a snapshot of our source link references; skip the packet based
  // sources, we don't know how to sample from them yet.
  FXL_DCHECK(source_link_refs_.size() == 0);
  {
    fbl::AutoLock links_lock(&links_lock_);
    for (auto& link : source_links_) {
      if (link->source_type() == AudioLink::SourceType::Packet) {
        continue;
      }
      source_link_refs_.push_back(link);
    }
  }

  // No matter what happens here, make certain that we are not holding any link
  // references in our snapshot when we are done.
  //
  // Note: We need to disable the clang static thread analysis code with this
  // lambda because clang is not able to know that...
  // 1) Once placed within the fbl::AutoCall, this cleanup routine cannot be
  //    transferred out of the scope of the MixToIntermediate function (so its
  //    life is bound to the scope of this function).
  // 2) Because of this, the AutoCall basically should inherit all of the thread
  //    analysis attributes of MixToIntermediate, including the assertion that
  //    MixToIntermediate is running in the mixer execution domain, which is
  //    what guards the source_link_refs_ member.
  // Because of this, we manually disable the thread analysis on this cleanup
  // lambda.
  auto release_snapshot_refs = fbl::MakeAutoCall(
      [this]() FXL_NO_THREAD_SAFETY_ANALYSIS { source_link_refs_.clear(); });

  // Silence our intermediate buffer.
  size_t job_bytes = sizeof(mix_buf_[0]) * mix_frames * format_->channels;
  ::memset(mix_buf_.get(), 0u, job_bytes);

  // If our current audio in gain is muted, we have nothing to do after filling
  // with silence.
  float capture_gain = db_gain_.load();
  if (capture_gain <= fuchsia::media::MUTED_GAIN) {
    return true;
  }

  bool accumulate = false;
  for (auto& link : source_link_refs_) {
    FXL_DCHECK(link->GetSource()->is_input() || link->GetSource()->is_output());

    // Get a hold of our device source (we know it is a device because this is a
    // ring buffer source, and ring buffer sources are always currently always)
    // and snapshot the current state of the ring buffer.
    auto device = static_cast<AudioDevice*>(link->GetSource().get());
    FXL_DCHECK(device != nullptr);

    // Right now, the only way for a device to not have a driver would be if it
    // was the throttle output.  Linking a audio in to the throttle output would
    // be a mistake.  For now if we detect this happening, log a warning, signal
    // an error and shut down.  Once MTWN-52 is resolved, we can come back here
    // and get rid of this.
    const auto& driver = device->driver();
    if (driver == nullptr) {
      FXL_LOG(ERROR) << "AudioIn appears to be linked to throttle output!  "
                        "Shutting down";
      return false;
    }

    // Figure out the fixed point gain scalar we will apply to this mix
    // operation by composing our gain with the link gain state.  The link's
    // gain helper class re-composes the source/dest gain combination if needed.
    Gain::AScale amplitude_scale = link->gain().GetGainScale(capture_gain);
    // If this gain scale is at or below our mute threshold, skip this source,
    // as it will not contribute to this mix pass.
    if (amplitude_scale <= Gain::MuteThreshold()) {
      continue;
    }

    AudioDriver::RingBufferSnapshot rb_snap;
    driver->SnapshotRingBuffer(&rb_snap);

    // If the driver does not have its ring buffer, or it does not have a valid
    // clock monotonic to ring buffer position transformation, then there is
    // nothing to do (at the moment).  Just skip this source and move on to the
    // next one.
    if ((rb_snap.ring_buffer == nullptr) ||
        (!rb_snap.clock_mono_to_ring_pos_bytes.invertable())) {
      continue;
    }

    // Now grab a hold of our capture link bookkeeping and update our
    // clock transformation if needed.
    CaptureLinkBookkeeping* bk =
        static_cast<CaptureLinkBookkeeping*>(link->bookkeeping().get());
    FXL_DCHECK(bk != nullptr);
    FXL_DCHECK(bk->mixer != nullptr);
    UpdateTransformation(bk, rb_snap);

    // TODO(johngro) : Much of the code after this is very similar to the logic
    // used to sample from packet sources (we basically model it as either 1 or
    // 2 packets, depending on which regions of the ring buffer are available to
    // be read from).  In the future, we should come back here and re-factor
    // this in such a way that we can sample from either packets or
    // ring-buffers, and so we can share the common logic with the output mixer
    // logic as well.
    //
    // Based on what time it is now, figure out what the safe portions of the
    // ring buffer are to read from.  Because it is a ring buffer, we may end up
    // with either one contiguous region of frames, or two contiguous regions
    // (split across the ring boundary).  Figure out the starting PTSs of these
    // regions (expressed in fractional start frames) in the process.
    const auto& rb = rb_snap.ring_buffer;
    zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
    int64_t end_fence_frames = bk->clock_mono_to_src_frames_fence.Apply(now);
    int64_t start_fence_frames =
        end_fence_frames - rb_snap.end_fence_to_start_fence_frames;
    start_fence_frames = std::max<int64_t>(start_fence_frames, 0);
    FXL_DCHECK(end_fence_frames >= 0);
    FXL_DCHECK(end_fence_frames - start_fence_frames < rb->frames());

    struct {
      uint32_t srb_pos;   // start ring buffer pos
      uint32_t len;       // region length in frames
      int64_t sfrac_pts;  // start fractional frame pts
    } regions[2];

    uint32_t start_frames_mod =
        static_cast<uint32_t>(start_fence_frames % rb->frames());
    uint32_t end_frames_mod =
        static_cast<uint32_t>(end_fence_frames % rb->frames());

    if (start_frames_mod <= end_frames_mod) {
      // One region
      regions[0].srb_pos = start_frames_mod;
      regions[0].len = end_frames_mod - start_frames_mod;
      regions[0].sfrac_pts = start_fence_frames << kPtsFractionalBits;
      regions[1].len = 0;
    } else {
      // Two regions
      regions[0].srb_pos = start_frames_mod;
      regions[0].len = rb->frames() - start_frames_mod;
      regions[0].sfrac_pts = start_fence_frames << kPtsFractionalBits;

      regions[1].srb_pos = 0;
      regions[1].len = end_frames_mod;
      regions[1].sfrac_pts =
          regions[0].sfrac_pts + (regions[0].len << kPtsFractionalBits);
    }

    uint32_t frames_left = mix_frames;
    float* buf = mix_buf_.get();

    // Now for each of the possible regions, intersect with our job and mix.
    for (const auto& region : regions) {
      // If we encounter a region of zero length, we are done.
      if (region.len == 0) {
        break;
      }

      // Figure out where the first and last sampling points of this job are,
      // expressed in fractional source frames
      FXL_DCHECK(frames_left > 0);
      const auto& trans = bk->dest_frames_to_frac_source_frames;
      int64_t job_start = trans.Apply(frame_count_ + mix_frames - frames_left);
      int64_t job_end = job_start + trans.rate().Scale(frames_left - 1);

      // Figure out the PTS of the final frame of audio in our source region
      int64_t efrac_pts = region.sfrac_pts + (region.len << kPtsFractionalBits);
      FXL_DCHECK((efrac_pts - region.sfrac_pts) >= Mixer::FRAC_ONE);
      int64_t final_pts = efrac_pts - Mixer::FRAC_ONE;

      // If the PTS of the final frame of audio in our source region is before
      // the negative window edge of our filter centered at our job's first
      // sampling point, then this source region is entirely in the past and may
      // be skipped.
      if (final_pts < (job_start - bk->mixer->neg_filter_width())) {
        continue;
      }

      // If the PTS of the first frame of audio in our source region is after
      // the positive window edge of our filter centered at our job's sampling
      // point, then source region is entirely in the future and we are done.
      if (region.sfrac_pts > (job_end + bk->mixer->pos_filter_width())) {
        break;
      }

      // Looks like the contents of this source region intersect our mixer's
      // filter.  Compute where in the intermediate buffer the first sample will
      // be produced, as well as where, relative to the start of the source
      // region, this sample will be taken from.
      int64_t source_offset_64 = job_start - region.sfrac_pts;
      int64_t output_offset_64 = 0;
      int64_t first_sample_pos_window_edge =
          job_start + bk->mixer->pos_filter_width();

      const TimelineRate& dest_to_src =
          bk->dest_frames_to_frac_source_frames.rate();
      // If first frame in this source region comes after positive edge of
      // filter window, we must skip output frames before producing data.
      if (region.sfrac_pts > first_sample_pos_window_edge) {
        int64_t src_to_skip = region.sfrac_pts - first_sample_pos_window_edge;

        // "+subject_delta-1" so that we 'round up' any fractional leftover.
        output_offset_64 = dest_to_src.Inverse().Scale(
            src_to_skip + dest_to_src.subject_delta() - 1);
        source_offset_64 += dest_to_src.Scale(output_offset_64);
      }

      FXL_DCHECK(output_offset_64 >= 0);
      FXL_DCHECK(output_offset_64 < static_cast<int64_t>(mix_frames));
      FXL_DCHECK(source_offset_64 <= std::numeric_limits<int32_t>::max());
      FXL_DCHECK(source_offset_64 >= std::numeric_limits<int32_t>::min());

      uint32_t region_frac_frame_len = region.len << kPtsFractionalBits;
      uint32_t output_offset = static_cast<uint32_t>(output_offset_64);
      int32_t frac_source_offset = static_cast<int32_t>(source_offset_64);

      FXL_DCHECK(frac_source_offset <
                 static_cast<int32_t>(region_frac_frame_len));

      const uint8_t* region_source =
          rb->virt() + (region.srb_pos * rb->frame_size());

      // Invalidate the region of the cache we are just about to read on
      // architectures who require it.
      //
      // TODO(johngro): Optimize this.  In particular...
      // 1) When we have multiple clients of this ring buffer, it would be good
      //    not to invalidate what has already been invalidated.
      // 2) If our driver's ring buffer is not being fed directly from hardware,
      //    there is no reason to invalidate the cache here.
      //
      // Also, at some point I need to come back and double check that the
      // mixer's filter width is being accounted for properly here.
      FXL_DCHECK(output_offset <= frames_left);
      uint64_t cache_target_frac_frames =
          dest_to_src.Scale(frames_left - output_offset);
      uint32_t cache_target_frames =
          ((cache_target_frac_frames - 1) >> kPtsFractionalBits) + 1;
      cache_target_frames = std::min(cache_target_frames, region.len);
      zx_cache_flush(region_source, cache_target_frames * rb->frame_size(),
                     ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);

      // Looks like we are ready to go. Mix.
      // TODO(mpuryear): integrate bookkeeping into the Mixer itself (MTWN-129).
      //
      // When calling Mix(), we communicate the resampling rate with three
      // parameters. We augment frac_step_size with modulo and denominator
      // arguments that capture the remaining rate component that cannot be
      // expressed by a 19.13 fixed-point step_size. Note: frac_step_size and
      // frac_input_offset use the same format -- they have the same limitations
      // in what they can and cannot communicate. This begs two questions:
      //
      // Q1: For perfect position accuracy, don't we also need an in/out param
      // to specify initial/final subframe modulo, for fractional source offset?
      // A1: Yes, for optimum position accuracy (within quantization limits), we
      // SHOULD incorporate running subframe position_modulo in this way.
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
      bool consumed_source = bk->mixer->Mix(
          buf, frames_left, &output_offset, region_source,
          region_frac_frame_len, &frac_source_offset, bk->step_size,
          amplitude_scale, accumulate, bk->modulo, bk->denominator());
      FXL_DCHECK(output_offset <= frames_left);

      if (!consumed_source) {
        // Looks like we didn't consume all of this region.  Assert that we
        // have produced all of our frames and we are done.
        FXL_DCHECK(output_offset == frames_left);
        break;
      }

      buf += output_offset * format_->channels;
      frames_left -= output_offset;
      if (!frames_left) {
        break;
      }
    }

    // We have now added something to the intermediate mix buffer.  For the next
    // source we process, we cannot assume that it is full of just silence.  Set
    // the accumulate flag so we tell the mixer to accumulate instead of just
    // overwrite.
    accumulate = true;
  }

  return true;
}

void AudioInImpl::UpdateTransformation(
    CaptureLinkBookkeeping* bk,
    const AudioDriver::RingBufferSnapshot& rb_snap) {
  FXL_DCHECK(bk != nullptr);

  if ((bk->dest_trans_gen_id == frames_to_clock_mono_gen_.get()) &&
      (bk->source_trans_gen_id == rb_snap.gen_id)) {
    return;
  }

  FXL_DCHECK(rb_snap.ring_buffer != nullptr);
  FXL_DCHECK(rb_snap.ring_buffer->frame_size() != 0);
  FXL_DCHECK(rb_snap.clock_mono_to_ring_pos_bytes.invertable());

  TimelineRate src_bytes_to_frac_frames(1u << kPtsFractionalBits,
                                        rb_snap.ring_buffer->frame_size());

  auto src_clock_mono_to_ring_pos_frac_frames =
      TimelineFunction::Compose(TimelineFunction(src_bytes_to_frac_frames),
                                rb_snap.clock_mono_to_ring_pos_bytes);

  bk->dest_frames_to_frac_source_frames = TimelineFunction::Compose(
      src_clock_mono_to_ring_pos_frac_frames, frames_to_clock_mono_);

  TimelineRate frac_frames_to_frames(1u, 1u << kPtsFractionalBits);
  int64_t offset = static_cast<int64_t>(rb_snap.position_to_end_fence_frames);
  bk->clock_mono_to_src_frames_fence = TimelineFunction::Compose(
      TimelineFunction(-offset, 0, frac_frames_to_frames),
      src_clock_mono_to_ring_pos_frac_frames);

  int64_t tmp_step_size = bk->dest_frames_to_frac_source_frames.rate().Scale(1);
  FXL_DCHECK(tmp_step_size >= 0);
  FXL_DCHECK(tmp_step_size <= std::numeric_limits<uint32_t>::max());
  bk->step_size = static_cast<uint32_t>(tmp_step_size);
  bk->modulo = bk->dest_frames_to_frac_source_frames.rate().subject_delta() -
               (bk->denominator() * bk->step_size);

  FXL_DCHECK(bk->denominator() > 0);
  bk->dest_trans_gen_id = frames_to_clock_mono_gen_.get();
  bk->source_trans_gen_id = rb_snap.gen_id;
}

void AudioInImpl::DoStopAsyncCapture() {
  // If this is being called, we had better be in the async stopping state.
  FXL_DCHECK(state_.load() == State::AsyncStopping);

  // Finish all pending buffers.  We should have at most one pending buffer.
  // Don't bother to move an empty buffer into the finished queue.  If there are
  // any buffers in the finished queue waiting to be sent back to the user, make
  // sure that the last one is flagged as the end of stream.
  {
    fbl::AutoLock pending_lock(&pending_lock_);

    if (!pending_capture_buffers_.is_empty()) {
      auto buf = pending_capture_buffers_.pop_front();

      // When we are in async mode, the Process method will attempt to keep
      // exactly one capture buffer in flight at all times, and never any more.
      // If we just popped that one buffer from the pending queue, we should be
      // able to DCHECK that the queue is now empty.
      FXL_CHECK(pending_capture_buffers_.is_empty());

      if (buf->filled_frames > 0) {
        finished_capture_buffers_.push_back(std::move(buf));
      }
    }
  }

  // Invalidate our clock transformation (the next packet we make will be
  // discontinuous).
  frames_to_clock_mono_ = TimelineFunction();
  frames_to_clock_mono_gen_.Next();

  // If we had a timer set, make sure that it is canceled.  There is no point in
  // having it armed right now as we are in the process of stopping.
  mix_timer_->Cancel();

  // Transition to the AsyncStoppingCallbackPending state, and signal the
  // service thread so it can complete the stop operation.
  state_.store(State::AsyncStoppingCallbackPending);
  owner_->ScheduleMainThreadTask(
      [thiz = fbl::WrapRefPtr(this)]() { thiz->FinishAsyncStopThunk(); });
}

bool AudioInImpl::QueueNextAsyncPendingBuffer() {
  // Sanity check our async offset bookkeeping.
  FXL_DCHECK(async_next_frame_offset_ < payload_buf_frames_);
  FXL_DCHECK(async_frames_per_packet_ <= (payload_buf_frames_ / 2));
  FXL_DCHECK(async_next_frame_offset_ <=
             (payload_buf_frames_ - async_frames_per_packet_));

  // Allocate bookkeeping to track this pending capture operation.  If we cannot
  // allocate a new pending capture buffer, it is a fatal error and we need to
  // start the process of shutting down.
  auto pending_capture_buffer = PcbAllocator::New(
      async_next_frame_offset_, async_frames_per_packet_, nullptr);
  if (pending_capture_buffer == nullptr) {
    FXL_LOG(ERROR) << "Failed to allocate pending capture buffer during async "
                      "capture mode!";
    ShutdownFromMixDomain();
    return false;
  }

  // Update our next frame offset.  If the new position of the next frame offset
  // does not leave enough room to produce another contiguous payload for our
  // user, reset the next frame offset to zero.  We made sure that we have space
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
    fbl::AutoLock pending_lock(&pending_lock_);
    pending_capture_buffers_.push_back(std::move(pending_capture_buffer));
  }
  return true;
}

void AudioInImpl::ShutdownFromMixDomain() {
  mix_domain_->DeactivateFromWithinDomain();
  state_.store(State::Shutdown);

  owner_->ScheduleMainThreadTask(
      [thiz = fbl::WrapRefPtr(this)]() { thiz->Shutdown(); });
}

void AudioInImpl::FinishAsyncStopThunk() {
  // Do nothing if we were shutdown between the time that this message was
  // posted to the main message loop and the time that we were dispatched.
  if (state_.load() == State::Shutdown) {
    return;
  }

  // Start by sending back all of our completed buffers.  Finish up by sending
  // an OnEndOfStream event.
  PcbList finished;
  {
    fbl::AutoLock pending_lock(&pending_lock_);
    FXL_DCHECK(pending_capture_buffers_.is_empty());
    finished = std::move(finished_capture_buffers_);
  }

  if (!finished.is_empty()) {
    FinishBuffers(std::move(finished));
  }

  binding_.events().OnEndOfStream();

  // If we have a valid callback to make, call it now.
  if (pending_async_stop_cbk_ != nullptr) {
    pending_async_stop_cbk_();
    pending_async_stop_cbk_ = nullptr;
  }

  // All done!  Transition back to the OperatingSync state.
  state_.store(State::OperatingSync);
}

void AudioInImpl::FinishBuffersThunk() {
  // Do nothing if we were shutdown between the time that this message was
  // posted to the main message loop and the time that we were dispatched.
  if (state_.load() == State::Shutdown) {
    return;
  }

  PcbList finished;
  {
    fbl::AutoLock pending_lock(&pending_lock_);
    finished = std::move(finished_capture_buffers_);
  }

  FinishBuffers(finished);
}

void AudioInImpl::FinishBuffers(const PcbList& finished_buffers) {
  for (const auto& finished_buffer : finished_buffers) {
    // If there is no callback tied to this buffer (meaning that it was
    // generated while operating in async mode), and it is not filled at all,
    // just skip it.
    if ((finished_buffer.cbk == nullptr) && !finished_buffer.filled_frames) {
      continue;
    }

    fuchsia::media::StreamPacket pkt;

    pkt.pts = finished_buffer.capture_timestamp;
    pkt.flags = finished_buffer.flags;
    pkt.payload_buffer_id = 0u;
    pkt.payload_offset = finished_buffer.offset_frames * bytes_per_frame_;
    pkt.payload_size = finished_buffer.filled_frames * bytes_per_frame_;

    if (finished_buffer.cbk != nullptr) {
      finished_buffer.cbk(std::move(pkt));
    } else {
      binding_.events().OnPacketProduced(std::move(pkt));
    }
  }
}

void AudioInImpl::UpdateFormat(fuchsia::media::AudioSampleFormat sample_format,
                               uint32_t channels, uint32_t frames_per_second) {
  // Record our new format.
  FXL_DCHECK(state_.load() == State::WaitingForVmo);
  format_->sample_format = sample_format;
  format_->channels = channels;
  format_->frames_per_second = frames_per_second;
  bytes_per_frame_ = channels * BytesPerSample(sample_format);

  // Pre-compute the ratio between frames and clock mono ticks.  Also figure out
  // the maximum number of frames we are allowed to mix and capture at a time.
  //
  // Some sources (like AudioOutputs) have a limited amount of time which they
  // are able to hold onto data after presentation.  We need to wait until after
  // presentation time to capture these frames, but if we batch up too much
  // work, then the AudioOutput may have overwritten the data before we decide
  // to get around to capturing it.  Limiting our maximum number of frames of to
  // capture to be less than this amount of time prevents this issue.
  //
  // TODO(johngro) : This constant does not belong here (and is not even
  // constant, strictly speaking).  We should move it somewhere else.
  constexpr int64_t kMaxTimePerCapture = ZX_MSEC(50);
  int64_t tmp;
  frames_to_clock_mono_rate_ =
      TimelineRate(ZX_SEC(1), format_->frames_per_second);
  tmp = frames_to_clock_mono_rate_.Inverse().Scale(kMaxTimePerCapture);
  max_frames_per_capture_ = static_cast<uint32_t>(tmp);

  FXL_DCHECK(tmp <= std::numeric_limits<uint32_t>::max());
  FXL_DCHECK(max_frames_per_capture_ > 0);
}

zx_status_t AudioInImpl::ChooseMixer(const std::shared_ptr<AudioLink>& link) {
  FXL_DCHECK(link != nullptr);

  const auto& source = link->GetSource();
  FXL_DCHECK(source);

  if (!source->is_input() && !source->is_output()) {
    FXL_LOG(INFO) << "Failed to find mixer for source of type "
                  << static_cast<uint32_t>(source->type());
    return ZX_ERR_INVALID_ARGS;
  }

  // The only devices who should not have a driver are the cursed throttle
  // output.
  auto device = static_cast<AudioDevice*>(source.get());
  if (device->driver() == nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  // Get the driver's currently configured format.  If it does not have one, we
  // cannot set up the mixer.
  fuchsia::media::AudioStreamTypePtr source_format;
  source_format = device->driver()->GetSourceFormat();
  if (!source_format) {
    FXL_LOG(INFO)
        << "Failed to find mixer.  Source currently has no configured format";
    return ZX_ERR_BAD_STATE;
  }

  // Extract our bookkeeping from the from the link, then set the mixer in it.
  FXL_DCHECK(link->bookkeeping() != nullptr);
  auto bk = static_cast<CaptureLinkBookkeeping*>(link->bookkeeping().get());

  FXL_DCHECK(bk->mixer == nullptr);
  bk->mixer = Mixer::Select(*source_format, *format_);
  if (bk->mixer == nullptr) {
    FXL_LOG(INFO) << "Failed to find mixer for audio in.";
    FXL_LOG(INFO) << "Source cfg: rate " << source_format->frames_per_second
                  << " ch " << source_format->channels << " sample fmt "
                  << fidl::ToUnderlying(source_format->sample_format);
    FXL_LOG(INFO) << "Dest cfg  : rate " << format_->frames_per_second << " ch "
                  << format_->channels << " sample fmt "
                  << fidl::ToUnderlying(format_->sample_format);
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

}  // namespace audio
}  // namespace media
