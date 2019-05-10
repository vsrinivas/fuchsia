// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_renderer_impl.h"

#include <lib/fit/defer.h>

#include "src/lib/fxl/arraysize.h"
#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/audio_core_impl.h"
#include "src/media/audio/audio_core/audio_output.h"

namespace media::audio {

fbl::RefPtr<AudioRendererImpl> AudioRendererImpl::Create(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer>
        audio_renderer_request,
    AudioCoreImpl* owner) {
  return fbl::AdoptRef(
      new AudioRendererImpl(std::move(audio_renderer_request), owner));
}

AudioRendererImpl::AudioRendererImpl(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer>
        audio_renderer_request,
    AudioCoreImpl* owner)
    : AudioObject(Type::AudioRenderer),
      owner_(owner),
      audio_renderer_binding_(this, std::move(audio_renderer_request)),
      pts_ticks_per_second_(1000000000, 1),
      ref_clock_to_frac_frames_(0, 0, {0, 1}) {
  audio_renderer_binding_.set_error_handler([this](zx_status_t status) {
    audio_renderer_binding_.Unbind();
    Shutdown();
  });
}

AudioRendererImpl::~AudioRendererImpl() {
  // Assert that we have been cleanly shutdown already.
  FXL_DCHECK(is_shutdown_);
  FXL_DCHECK(!audio_renderer_binding_.is_bound());
  FXL_DCHECK(gain_control_bindings_.size() == 0);
}

void AudioRendererImpl::Shutdown() {
  // If we have already been shutdown, then we are just waiting for the service
  // to destroy us. Run some FXL_DCHECK sanity checks and get out.
  if (is_shutdown_) {
    FXL_DCHECK(!audio_renderer_binding_.is_bound());
    return;
  }

  is_shutdown_ = true;

  PreventNewLinks();
  Unlink();
  UnlinkThrottle();

  if (audio_renderer_binding_.is_bound()) {
    audio_renderer_binding_.Unbind();
  }

  gain_control_bindings_.CloseAll();
  payload_buffer_.reset();

  // Make sure we have left the set of active AudioRenderers.
  if (InContainer()) {
    owner_->GetDeviceManager().RemoveAudioRenderer(this);
  }
}

void AudioRendererImpl::SnapshotCurrentTimelineFunction(int64_t reference_time,
                                                        TimelineFunction* out,
                                                        uint32_t* generation) {
  FXL_DCHECK(out != nullptr);
  FXL_DCHECK(generation != nullptr);

  fbl::AutoLock lock(&ref_to_ff_lock_);
  *out = ref_clock_to_frac_frames_;
  *generation = ref_clock_to_frac_frames_gen_.get();
}

void AudioRendererImpl::SetThrottleOutput(
    fbl::RefPtr<AudioLinkPacketSource> throttle_output_link) {
  FXL_DCHECK(throttle_output_link != nullptr);
  FXL_DCHECK(throttle_output_link_ == nullptr);
  throttle_output_link_ = std::move(throttle_output_link);
}

void AudioRendererImpl::RecomputeMinClockLeadTime() {
  int64_t cur_lead_time = 0;

  ForEachDestLink([throttle_ptr = throttle_output_link_.get(),
                   &cur_lead_time](auto& link) {
    if (link.GetDest()->is_output() && &link != throttle_ptr) {
      const auto output = fbl::RefPtr<AudioOutput>::Downcast(link.GetDest());

      cur_lead_time =
          std::max(cur_lead_time, output->min_clock_lead_time_nsec());
    }
  });

  if (min_clock_lead_nsec_ != cur_lead_time) {
    min_clock_lead_nsec_ = cur_lead_time;
    ReportNewMinClockLeadTime();
  }
}

// IsOperating is true any time we have any packets in flight. Most
// configuration functions cannot be called any time we are operational.
bool AudioRendererImpl::IsOperating() {
  if (throttle_output_link_ && !throttle_output_link_->pending_queue_empty()) {
    return true;
  }

  return ForAnyDestLink([](auto& link) {
    // If pending queue empty: this link is NOT operating; ask other links.
    // Else: Link IS operating; final answer is YES; no need to ask others.
    return !AsPacketSource(link).pending_queue_empty();
  });
}

bool AudioRendererImpl::ValidateConfig() {
  if (config_validated_) {
    return true;
  }

  if (!format_info_valid() || (payload_buffer_ == nullptr)) {
    return false;
  }

  // Compute the number of fractional frames per PTS tick.
  uint32_t fps = format_info()->format().frames_per_second;
  uint32_t frac_fps = fps << kPtsFractionalBits;
  frac_frames_per_pts_tick_ = TimelineRate::Product(
      pts_ticks_per_second_.Inverse(), TimelineRate(frac_fps, 1));

  // Compute the PTS continuity threshold expressed in fractional input frames.
  if (pts_continuity_threshold_set_) {
    // The user has not explicitly set a continuity threshold. Default to 1/2
    // of a PTS tick expressed in fractional input frames, rounded up.
    pts_continuity_threshold_frac_frame_ =
        (frac_frames_per_pts_tick_.Scale(1) + 1) >> 1;
  } else {
    pts_continuity_threshold_frac_frame_ =
        static_cast<double>(frac_fps) * pts_continuity_threshold_;
  }

  // Compute the number of fractional frames per reference clock tick.
  //
  // TODO(mpuryear): handle the case where the reference clock nominal rate is
  // something other than CLOCK_MONOTONIC
  frac_frames_per_ref_tick_ = TimelineRate(frac_fps, 1000000000u);

  // TODO(mpuryear): Precompute anything else needed here. Adding links to other
  // outputs (and selecting resampling filters) might belong here as well.

  config_validated_ = true;
  return true;
}

void AudioRendererImpl::ComputePtsToFracFrames(int64_t first_pts) {
  // We should not be calling this, if transformation is already valid.
  FXL_DCHECK(!pts_to_frac_frames_valid_);
  pts_to_frac_frames_ = TimelineFunction(next_frac_frame_pts_, first_pts,
                                         frac_frames_per_pts_tick_);
  pts_to_frac_frames_valid_ = true;
}

void AudioRendererImpl::UnlinkThrottle() {
  if (throttle_output_link_ != nullptr) {
    RemoveLink(throttle_output_link_);
    throttle_output_link_.reset();
  }
}

////////////////////////////////////////////////////////////////////////////////
//
// AudioRenderer Interface
//
void AudioRendererImpl::SetPcmStreamType(
    fuchsia::media::AudioStreamType format) {
  auto cleanup = fit::defer([this]() { Shutdown(); });

  // We cannot change the format while we are currently operational
  if (IsOperating()) {
    FXL_LOG(ERROR) << "Attempted to set format while in the operational mode.";
    return;
  }

  // Sanity check the requested format
  switch (format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
    case fuchsia::media::AudioSampleFormat::FLOAT:
      break;

    default:
      FXL_LOG(ERROR)
          << "Unsupported sample format ("
          << fidl::ToUnderlying(format.sample_format)
          << ") in fuchsia::media::AudioRendererImpl::SetPcmStreamType.";
      return;
  }

  if ((format.channels < fuchsia::media::MIN_PCM_CHANNEL_COUNT) ||
      (format.channels > fuchsia::media::MAX_PCM_CHANNEL_COUNT)) {
    FXL_LOG(ERROR)
        << "Invalid channel count (" << format.channels
        << ") in fuchsia::media::AudioRendererImpl::SetPcmStreamType. Must "
           "be in the range ["
        << fuchsia::media::MIN_PCM_CHANNEL_COUNT << ", "
        << fuchsia::media::MAX_PCM_CHANNEL_COUNT << "]";
    return;
  }

  if ((format.frames_per_second < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) ||
      (format.frames_per_second > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)) {
    FXL_LOG(ERROR)
        << "Invalid frame rate (" << format.frames_per_second
        << ") in fuchsia::media::AudioRendererImpl::SetPcmStreamType. Must "
           "be in the range ["
        << fuchsia::media::MIN_PCM_FRAMES_PER_SECOND << ", "
        << fuchsia::media::MAX_PCM_FRAMES_PER_SECOND << "]";
    return;
  }

  // Everything checks out. Discard any existing links we hold (including
  // throttle output). New links need to be created with our new format.
  Unlink();
  UnlinkThrottle();

  // Create a new format info object so we can create links to outputs.
  // TODO(mpuryear): Consider consolidating most of the format_info class.
  fuchsia::media::AudioStreamType cfg;
  cfg.sample_format = format.sample_format;
  cfg.channels = format.channels;
  cfg.frames_per_second = format.frames_per_second;
  format_info_ = AudioRendererFormatInfo::Create(cfg);

  // Have the device manager initialize our set of outputs. Note: we currently
  // need no lock here. Method calls from user-facing interfaces are serialized
  // by the FIDL framework, and none of the manager's threads should ever need
  // to manipulate the set. Cleanup of outputs which have gone away is currently
  // handled in a lazy fashion when the AudioRenderer fails to promote its weak
  // reference during an operation involving its outputs.
  //
  // TODO(mpuryear): someday, deal with recalculating properties that depend on
  // an AudioRenderer's current set of outputs (for example, minimum latency).
  // This will probably be done using a dirty flag in the AudioRenderer
  // implementation, scheduling a job to recalculate properties for dirty
  // AudioRenderers, and notifying users as appropriate.

  // If we cannot promote our own weak pointer, something is seriously wrong.
  owner_->GetDeviceManager().SelectOutputsForAudioRenderer(this);

  // Things went well, cancel the cleanup hook. If our config had been
  // validated previously, it will have to be revalidated as we move into the
  // operational phase of our life.
  config_validated_ = false;
  cleanup.cancel();
}

void AudioRendererImpl::SetStreamType(fuchsia::media::StreamType stream_type) {
  FXL_LOG(ERROR) << "SetStreamType is not currently supported.";
  Shutdown();
}

void AudioRendererImpl::AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) {
  if (id != 0) {
    FXL_LOG(ERROR) << "Only buffer ID 0 is currently supported.";
    Shutdown();
    return;
  }

  auto cleanup = fit::defer([this]() { Shutdown(); });

  if (IsOperating()) {
    FXL_LOG(ERROR)
        << "Attempted to set payload buffer while in the operational mode.";
    return;
  }

  // TODO(johngro) : MTWN-69
  // Map this into a sub-vmar instead of defaulting to the root
  // once teisenbe@ provides guidance on the best-practice for doing this.
  zx_status_t res;
  payload_buffer_ = fbl::MakeRefCounted<RefCountedVmoMapper>();
  res = payload_buffer_->Map(payload_buffer, 0, 0, ZX_VM_PERM_READ);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map payload buffer (res = " << res << ")";
    return;
  }

  // Things went well, cancel the cleanup hook. If our config had been
  // validated previously, it will have to be revalidated as we move into the
  // operational phase of our life.
  config_validated_ = false;
  cleanup.cancel();
}

void AudioRendererImpl::RemovePayloadBuffer(uint32_t id) {
  FXL_LOG(ERROR) << "RemovePayloadBuffer is not currently supported.";
  Shutdown();
}

void AudioRendererImpl::SetPtsUnits(uint32_t tick_per_second_numerator,
                                    uint32_t tick_per_second_denominator) {
  auto cleanup = fit::defer([this]() { Shutdown(); });

  if (IsOperating()) {
    FXL_LOG(ERROR)
        << "Attempted to set PTS units while in the operational mode.";
    return;
  }

  if (!tick_per_second_numerator || !tick_per_second_denominator) {
    FXL_LOG(ERROR) << "Bad PTS ticks per second (" << tick_per_second_numerator
                   << "/" << tick_per_second_denominator << ")";
    return;
  }

  pts_ticks_per_second_ =
      TimelineRate(tick_per_second_numerator, tick_per_second_denominator);

  // Things went well, cancel the cleanup hook. If our config had been
  // validated previously, it will have to be revalidated as we move into the
  // operational phase of our life.
  config_validated_ = false;
  cleanup.cancel();
}

void AudioRendererImpl::SetPtsContinuityThreshold(float threshold_seconds) {
  auto cleanup = fit::defer([this]() { Shutdown(); });

  if (IsOperating()) {
    FXL_LOG(ERROR)
        << "Attempted to set PTS cont threshold while in the operational mode.";
    return;
  }

  if (threshold_seconds < 0.0) {
    FXL_LOG(ERROR) << "Invalid PTS continuity threshold (" << threshold_seconds
                   << ")";
    return;
  }

  pts_continuity_threshold_ = threshold_seconds;
  pts_continuity_threshold_set_ = true;

  // Things went well, cancel the cleanup hook. If our config had been
  // validated previously, it will have to be revalidated as we move into the
  // operational phase of our life.
  config_validated_ = false;
  cleanup.cancel();
}

void AudioRendererImpl::SetReferenceClock(zx::handle ref_clock) {
  auto cleanup = fit::defer([this]() { Shutdown(); });

  if (IsOperating()) {
    FXL_LOG(ERROR)
        << "Attempted to set reference clock while in the operational mode.";
    return;
  }

  FXL_NOTIMPLEMENTED();
}

void AudioRendererImpl::SendPacket(fuchsia::media::StreamPacket packet,
                                   SendPacketCallback callback) {
  auto cleanup = fit::defer([this]() { Shutdown(); });

  // It is an error to attempt to send a packet before we have established at
  // least a minimum valid configuration. IOW - the format must have been
  // configured, and we must have an established payload buffer.
  if (!ValidateConfig()) {
    FXL_LOG(ERROR) << "Failed to validate configuration during SendPacket";
    return;
  }

  // Start by making sure that the region we are receiving is made from an
  // integral number of audio frames. Count the total number of frames in the
  // process.
  uint32_t frame_size = format_info()->bytes_per_frame();
  FXL_DCHECK(frame_size != 0);
  if (packet.payload_size % frame_size) {
    FXL_LOG(ERROR) << "Region length (" << packet.payload_size
                   << ") is not divisible by by audio frame size ("
                   << frame_size << ")";
    return;
  }

  // Make sure that we don't exceed the maximum permissible frames-per-packet.
  static constexpr uint32_t kMaxFrames =
      std::numeric_limits<uint32_t>::max() >> kPtsFractionalBits;
  uint32_t frame_count = (packet.payload_size / frame_size);
  if (frame_count > kMaxFrames) {
    FXL_LOG(ERROR) << "Audio frame count (" << frame_count
                   << ") exceeds maximum allowed (" << kMaxFrames << ")";
    return;
  }

  // Make sure that the packet offset/size exists entirely within the payload
  // buffer.
  FXL_DCHECK(payload_buffer_ != nullptr);
  uint64_t start = packet.payload_offset;
  uint64_t end = start + packet.payload_size;
  uint64_t pb_size = payload_buffer_->size();
  if ((start >= pb_size) || (end > pb_size)) {
    FXL_LOG(ERROR) << "Bad packet range [" << start << ", " << end
                   << "). Payload buffer size is " << pb_size;
    return;
  }

  // Compute the PTS values for this packet applying our interpolation and
  // continuity thresholds as we go. Start by checking to see if this our PTS
  // to frames transformation needs to be computed (this should be needed after
  // startup, and after each flush operation).
  if (!pts_to_frac_frames_valid_) {
    ComputePtsToFracFrames(
        (packet.pts == fuchsia::media::NO_TIMESTAMP) ? 0 : packet.pts);
  }

  // Now compute the starting PTS expressed in fractional input frames. If no
  // explicit PTS was provided, interpolate using the next expected PTS.
  int64_t start_pts;
  if (packet.pts == fuchsia::media::NO_TIMESTAMP) {
    start_pts = next_frac_frame_pts_;
  } else {
    // Looks like we have an explicit PTS on this packet. Boost it into the
    // fractional input frame domain, then apply our continuity threshold rules.
    int64_t packet_ffpts = pts_to_frac_frames_.Apply(packet.pts);
    int64_t delta = std::abs(packet_ffpts - next_frac_frame_pts_);
    start_pts = (delta < pts_continuity_threshold_frac_frame_)
                    ? next_frac_frame_pts_
                    : packet_ffpts;
  }

  // Snap the starting pts to an input frame boundary.
  //
  // TODO(johngro): Don't do this. If a user wants to write an explicit
  // timestamp on an input packet which schedules the packet to start at a
  // fractional position on the input time line, we should probably permit this.
  // We need to make sure that the mixer cores are ready to handle this case
  // before proceeding, however. See MTWN-88
  constexpr auto mask = ~((static_cast<int64_t>(1) << kPtsFractionalBits) - 1);
  start_pts &= mask;

  // Create the packet.
  auto packet_ref = fbl::MakeRefCounted<AudioPacketRef>(
      payload_buffer_, std::move(callback), packet, owner_,
      frame_count << kPtsFractionalBits, start_pts);

  // The end pts is the value we will use for the next packet's start PTS, if
  // the user does not provide an explicit PTS.
  next_frac_frame_pts_ = packet_ref->end_pts();

  // Distribute our packet to all our dest links
  ForEachDestLink([moved_packet = std::move(packet_ref)](auto& link) {
    AsPacketSource(link).PushToPendingQueue(std::move(moved_packet));
  });

  // Things went well, cancel the cleanup hook.
  cleanup.cancel();
}

void AudioRendererImpl::SendPacketNoReply(fuchsia::media::StreamPacket packet) {
  SendPacket(packet, nullptr);
}

void AudioRendererImpl::EndOfStream() {
  // Does nothing.
}

void AudioRendererImpl::DiscardAllPackets(DiscardAllPacketsCallback callback) {
  // If the user has requested a callback, create the flush token we will use to
  // invoke the callback at the proper time.
  fbl::RefPtr<PendingFlushToken> flush_token;
  if (callback != nullptr) {
    flush_token = PendingFlushToken::Create(owner_, std::move(callback));
  }

  // Tell each link to flush. If link is currently processing pending data, it
  // will take a reference to the flush token and ensure a callback is queued at
  // the proper time (after all pending packet-complete callbacks are queued).
  ForEachDestLink([moved_token = std::move(flush_token)](auto& link) {
    AsPacketSource(link).FlushPendingQueue(std::move(moved_token));
  });

  // Invalidate any internal state which gets reset after a flush.
  next_frac_frame_pts_ = 0;
  pts_to_frac_frames_valid_ = false;
  pause_time_frac_frames_valid_ = false;
}

void AudioRendererImpl::DiscardAllPacketsNoReply() {
  DiscardAllPackets(nullptr);
}

void AudioRendererImpl::Play(int64_t reference_time, int64_t media_time,
                             PlayCallback callback) {
  auto cleanup = fit::defer([this]() { Shutdown(); });

  if (!ValidateConfig()) {
    FXL_LOG(ERROR) << "Failed to validate configuration during Play";
    return;
  }

  // TODO(johngro): What do we want to do here if we are already playing?

  // Did the user supply a reference time? If not, figure out a safe starting
  // time based on the outputs we are currently linked to.
  //
  // TODO(johngro): We need to use our reference clock here, and not just assume
  // clock monotonic is our reference clock.
  if (reference_time == fuchsia::media::NO_TIMESTAMP) {
    // TODO(johngro): How much more than the minimum clock lead time do we want
    // to pad this by? Also, if/when lead time requirements change, do we want
    // to introduce a discontinuity?
    //
    // Perhaps we should consider an explicit mode (make it the default) where
    // timing across outputs is considered to be loose. In particular, make no
    // effort to take external latency into account, and no effort to
    // synchronize streams across multiple parallel outputs. In a world like
    // this, we might need to update this lead time beacuse of a change in
    // internal interconnect requirements, but in general, the impact should
    // usually be pretty small since internal requirements for lead times tend
    // to be small, (while external requirements can be huge).
    constexpr int64_t lead_time_padding = ZX_MSEC(20);
    reference_time =
        zx_clock_get_monotonic() + lead_time_padding + min_clock_lead_nsec_;
  }

  // If the user did not specify a media time, use the media time of the first
  // packet in the pending queue.
  //
  // Note: media times specified by the user are expressed in the PTS units they
  // specified using SetPtsUnits (or nanosecond units by default). Internally,
  // we stamp all of our payloads in fractional input frames on a timeline
  // defined when we transition to our operational mode. We need to remember to
  // translate back and forth as appropriate.
  int64_t frac_frame_media_time;
  if (media_time == fuchsia::media::NO_TIMESTAMP) {
    // Are we resuming from pause?
    if (pause_time_frac_frames_valid_) {
      frac_frame_media_time = pause_time_frac_frames_;
    } else {
      // TODO(johngro): peek the first PTS of the pending queue.
      frac_frame_media_time = 0;
    }

    // If we do not know the pts_to_frac_frames relationship yet, compute one.
    if (!pts_to_frac_frames_valid_) {
      next_frac_frame_pts_ = frac_frame_media_time;
      ComputePtsToFracFrames(0);
    }

    media_time = pts_to_frac_frames_.ApplyInverse(frac_frame_media_time);
  } else {
    // If we do not know the pts_to_frac_frames relationship yet, compute one.
    if (!pts_to_frac_frames_valid_) {
      ComputePtsToFracFrames(media_time);
      frac_frame_media_time = next_frac_frame_pts_;
    } else {
      frac_frame_media_time = pts_to_frac_frames_.Apply(media_time);
    }
  }

  // Update our transformation.
  //
  // TODO(johngro): if we need to trigger a remix for our set of outputs, here
  // is the place to do it.
  {
    fbl::AutoLock lock(&ref_to_ff_lock_);
    ref_clock_to_frac_frames_ = TimelineFunction(
        frac_frame_media_time, reference_time, frac_frames_per_ref_tick_);
    ref_clock_to_frac_frames_gen_.Next();
  }

  // If the user requested a callback, invoke it now.
  if (callback != nullptr) {
    callback(reference_time, media_time);
  }

  // Things went well, cancel the cleanup hook.
  cleanup.cancel();
}

void AudioRendererImpl::PlayNoReply(int64_t reference_time,
                                    int64_t media_time) {
  Play(reference_time, media_time, nullptr);
}

void AudioRendererImpl::Pause(PauseCallback callback) {
  auto cleanup = fit::defer([this]() { Shutdown(); });

  if (!ValidateConfig()) {
    FXL_LOG(ERROR) << "Failed to validate configuration during Pause";
    return;
  }

  // Update our reference clock to fractional frame transformation, making sure
  // to keep it 1st order continuous in the process.
  int64_t ref_clock_now;
  {
    fbl::AutoLock lock(&ref_to_ff_lock_);

    // TODO(johngro): query the actual reference clock, do not assume that
    // CLOCK_MONO is the reference clock.
    ref_clock_now = zx_clock_get_monotonic();
    pause_time_frac_frames_ = ref_clock_to_frac_frames_.Apply(ref_clock_now);
    pause_time_frac_frames_valid_ = true;

    ref_clock_to_frac_frames_ =
        TimelineFunction(pause_time_frac_frames_, ref_clock_now, {0, 1});
    ref_clock_to_frac_frames_gen_.Next();
  }

  // If we do not know the pts_to_frac_frames relationship yet, compute one.
  if (!pts_to_frac_frames_valid_) {
    next_frac_frame_pts_ = pause_time_frac_frames_;
    ComputePtsToFracFrames(0);
  }

  // If the user requested a callback, figure out the media time that we paused
  // at and report back.
  if (callback != nullptr) {
    int64_t paused_media_time =
        pts_to_frac_frames_.ApplyInverse(pause_time_frac_frames_);
    callback(ref_clock_now, paused_media_time);
  }

  // Things went well, cancel the cleanup hook.
  cleanup.cancel();
}

void AudioRendererImpl::PauseNoReply() { Pause(nullptr); }

// Set the stream gain, in each Renderer -> Output audio path. The Gain object
// contains multiple stages. In playback, renderer gain is pre-mix and hence is
// "source" gain; the Output device (or master) gain is "dest" gain.
void AudioRendererImpl::SetGain(float gain_db) {
  // Anywhere we set stream_gain_db_, we should perform this range check.
  if (gain_db > fuchsia::media::audio::MAX_GAIN_DB ||
      gain_db < fuchsia::media::audio::MUTED_GAIN_DB || isnan(gain_db)) {
    FXL_LOG(ERROR) << "SetGain(" << gain_db << " dB) out of range.";
    Shutdown();  // Use fit::defer() pattern if more than 1 error return case.
    return;
  }

  if (stream_gain_db_ == gain_db) {
    return;
  }

  stream_gain_db_ = gain_db;

  // Set this gain with every link (except the link to throttle output)
  ForEachDestLink(
      [throttle_ptr = throttle_output_link_.get(), gain_db](auto& link) {
        if (&link != throttle_ptr) {
          link.bookkeeping()->gain.SetSourceGain(gain_db);
        }
      });

  NotifyGainMuteChanged();
}

// Set a stream gain ramp, in each Renderer -> Output audio path. Renderer gain
// is pre-mix and hence is the Source component in the Gain object.
void AudioRendererImpl::SetGainWithRamp(
    float gain_db, zx_duration_t duration_ns,
    fuchsia::media::audio::RampType ramp_type) {
  if (gain_db > fuchsia::media::audio::MAX_GAIN_DB ||
      gain_db < fuchsia::media::audio::MUTED_GAIN_DB || isnan(gain_db)) {
    FXL_LOG(ERROR) << "SetGainWithRamp(" << gain_db << " dB) out of range.";
    Shutdown();  // Use fit::defer() pattern if more than 1 error return case.
    return;
  }

  ForEachDestLink([throttle_ptr = throttle_output_link_.get(), gain_db,
                   duration_ns, ramp_type](auto& link) {
    if (&link != throttle_ptr) {
      link.bookkeeping()->gain.SetSourceGainWithRamp(gain_db, duration_ns,
                                                      ramp_type);
    }
  });

  // TODO(mpuryear): implement notifications for gain ramps.
}

// Set a stream mute, in each Renderer -> Output audio path. For now, mute is
// handled by setting gain to a value guaranteed to be silent, but going forward
// we may pass this thru to the Gain object. Renderer gain/mute is pre-mix and
// hence is the Source component in the Gain object.
void AudioRendererImpl::SetMute(bool mute) {
  // Only do the work if the request represents a change in state.
  if (mute_ == mute) {
    return;
  }

  mute_ = mute;

  ForEachDestLink(
      [throttle_ptr = throttle_output_link_.get(), mute](auto& link) {
        if (&link != throttle_ptr) {
          link.bookkeeping()->gain.SetSourceMute(mute);
        }
      });

  NotifyGainMuteChanged();
}

void AudioRendererImpl::BindGainControl(
    fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) {
  gain_control_bindings_.AddBinding(GainControlBinding::Create(this),
                                    std::move(request));
}

void AudioRendererImpl::EnableMinLeadTimeEvents(bool enabled) {
  min_clock_lead_time_events_enabled_ = enabled;
  ReportNewMinClockLeadTime();
}

void AudioRendererImpl::GetMinLeadTime(GetMinLeadTimeCallback callback) {
  callback(min_clock_lead_nsec_);
}

void AudioRendererImpl::ReportNewMinClockLeadTime() {
  if (min_clock_lead_time_events_enabled_) {
    auto& lead_time_event = audio_renderer_binding_.events();
    lead_time_event.OnMinLeadTimeChanged(min_clock_lead_nsec_);
  }
}

void AudioRendererImpl::NotifyGainMuteChanged() {
  // TODO(mpuryear): consider whether GainControl events should be disable-able,
  // not unlike MinLeadTime events.
  for (auto& gain_binding : gain_control_bindings_.bindings()) {
    gain_binding->events().OnGainMuteChanged(stream_gain_db_, mute_);
  }
}

// Shorthand to save horizontal space for the thunks which follow.
void AudioRendererImpl::GainControlBinding::SetGain(float gain_db) {
  owner_->SetGain(gain_db);
}

void AudioRendererImpl::GainControlBinding::SetGainWithRamp(
    float gain_db, zx_duration_t duration_ns,
    fuchsia::media::audio::RampType ramp_type) {
  owner_->SetGainWithRamp(gain_db, duration_ns, ramp_type);
}

void AudioRendererImpl::GainControlBinding::SetMute(bool mute) {
  owner_->SetMute(mute);
}

}  // namespace media::audio
