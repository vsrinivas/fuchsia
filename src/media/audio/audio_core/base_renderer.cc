// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/base_renderer.h"

#include <lib/fit/defer.h>

#include "src/lib/fxl/arraysize.h"
#include "src/media/audio/audio_core/audio_core_impl.h"
#include "src/media/audio/audio_core/audio_output.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {
namespace {

// If client does not specify a ref_time for Play, pad it by this amount
constexpr zx::duration kPaddingForUnspecifiedRefTime = zx::msec(20);

// Short-term workaround (until clients who timestamp are updated): if a client
// specifies ref_time but uses PlayNoReply (thus doesn't want the callback
// telling them what actual ref_time was), secretly pad by this amount.
constexpr zx::duration kPaddingForPlayNoReplyWithRefTime = zx::msec(10);

// 4 slabs will allow each renderer to create >500 packets. Any client creating any more packets
// than this that are outstanding at the same time will be disconnected.
constexpr size_t kMaxPacketAllocatorSlabs = 4;

}  // namespace

BaseRenderer::BaseRenderer(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request, Context* context)
    : AudioObject(Type::AudioRenderer),
      context_(*context),
      audio_renderer_binding_(this, std::move(audio_renderer_request)),
      pts_ticks_per_second_(1000000000, 1),
      reference_clock_to_fractional_frames_(fbl::MakeRefCounted<VersionedTimelineFunction>()),
      packet_allocator_(kMaxPacketAllocatorSlabs, true) {
  TRACE_DURATION("audio", "BaseRenderer::BaseRenderer");
  FX_DCHECK(context);
  REP(AddingRenderer(*this));
  AUD_VLOG_OBJ(TRACE, this);

  audio_renderer_binding_.set_error_handler([this](zx_status_t status) {
    TRACE_DURATION("audio", "BaseRenderer::audio_renderer_binding_.error_handler", "zx_status",
                   status);
    FX_PLOGS(INFO, status) << "Client disconnected";
    context_.route_graph().RemoveRenderer(*this);
  });
}

BaseRenderer::~BaseRenderer() {
  AUD_VLOG_OBJ(TRACE, this);

  wav_writer_.Close();
  payload_buffers_.clear();
  REP(RemovingRenderer(*this));
}

void BaseRenderer::Shutdown() {
  TRACE_DURATION("audio", "BaseRenderer::Shutdown");
  AUD_VLOG_OBJ(TRACE, this);

  ReportStop();

  wav_writer_.Close();
  payload_buffers_.clear();
}

fit::result<std::shared_ptr<Stream>, zx_status_t> BaseRenderer::InitializeDestLink(
    const AudioObject& dest) {
  TRACE_DURATION("audio", "BaseRenderer::InitializeDestLink");
  auto queue = std::make_shared<PacketQueue>(*format(), reference_clock_to_fractional_frames_);
  packet_queues_.insert({&dest, queue});
  return fit::ok(std::move(queue));
}

void BaseRenderer::CleanupDestLink(const AudioObject& dest) {
  TRACE_DURATION("audio", "BaseRenderer::CleanupDestLink");
  auto it = packet_queues_.find(&dest);
  FX_CHECK(it != packet_queues_.end());
  packet_queues_.erase(it);
}

void BaseRenderer::RecomputeMinLeadTime() {
  TRACE_DURATION("audio", "BaseRenderer::RecomputeMinLeadTime");
  zx::duration cur_lead_time;

  context_.link_matrix().ForEachDestLink(*this, [&cur_lead_time](LinkMatrix::LinkHandle link) {
    const auto& output = static_cast<const AudioDevice&>(*link.object);

    cur_lead_time = std::max(cur_lead_time, output.min_lead_time());
  });

  if (min_lead_time_ != cur_lead_time) {
    REP(SettingRendererMinLeadTime(*this, cur_lead_time));
    min_lead_time_ = cur_lead_time;
    ReportNewMinLeadTime();
  }
}

// IsOperating is true any time we have any packets in flight. Configuration functions cannot be
// called any time we are operational.
bool BaseRenderer::IsOperating() {
  TRACE_DURATION("audio", "BaseRenderer::IsOperating");

  for (const auto& [_, packet_queue] : packet_queues_) {
    // If the packet queue is not empty then this link _is_ operating.
    if (!packet_queue->empty()) {
      return true;
    }
  }
  return false;
}

bool BaseRenderer::ValidateConfig() {
  TRACE_DURATION("audio", "BaseRenderer::ValidateConfig");
  if (config_validated_) {
    return true;
  }

  if (!format_valid() || payload_buffers_.empty()) {
    return false;
  }

  // Compute the number of fractional frames per PTS tick.
  FractionalFrames<uint32_t> frac_fps(format()->stream_type().frames_per_second);
  frac_frames_per_pts_tick_ =
      TimelineRate::Product(pts_ticks_per_second_.Inverse(), TimelineRate(frac_fps.raw_value(), 1));

  // Compute the PTS continuity threshold expressed in fractional input frames.
  if (!pts_continuity_threshold_set_) {
    // The user has not explicitly set a continuity threshold. Default to 1/2
    // of a PTS tick expressed in fractional input frames, rounded up.
    pts_continuity_threshold_frac_frame_ =
        FractionalFrames<int64_t>::FromRaw((frac_frames_per_pts_tick_.Scale(1) + 1) >> 1);
  } else {
    pts_continuity_threshold_frac_frame_ = FractionalFrames<int64_t>::FromRaw(
        static_cast<double>(frac_fps.raw_value()) * pts_continuity_threshold_);
  }

  AUD_VLOG_OBJ(TRACE, this) << " threshold_set_: " << pts_continuity_threshold_set_
                            << ", thres_frac_frame_: " << std::hex
                            << pts_continuity_threshold_frac_frame_.raw_value();

  // Compute the number of fractional frames per reference clock tick.
  //
  // TODO(mpuryear): handle the case where the reference clock nominal rate is
  // something other than CLOCK_MONOTONIC
  frac_frames_per_ref_tick_ = TimelineRate(frac_fps.raw_value(), 1000000000u);

  // TODO(mpuryear): Precompute anything else needed here. Adding links to other
  // outputs (and selecting resampling filters) might belong here as well.

  // Initialize the WavWriter here.
  wav_writer_.Initialize(nullptr, format()->stream_type().sample_format,
                         format()->stream_type().channels,
                         format()->stream_type().frames_per_second,
                         (format()->bytes_per_frame() * 8) / format()->stream_type().channels);

  config_validated_ = true;
  return true;
}

void BaseRenderer::ComputePtsToFracFrames(int64_t first_pts) {
  TRACE_DURATION("audio", "BaseRenderer::ComputePtsToFracFrames");
  // We should not be calling this, if transformation is already valid.
  FX_DCHECK(!pts_to_frac_frames_valid_);

  pts_to_frac_frames_ =
      TimelineFunction(next_frac_frame_pts_.raw_value(), first_pts, frac_frames_per_pts_tick_);
  pts_to_frac_frames_valid_ = true;

  AUD_VLOG_OBJ(TRACE, this) << " (" << first_pts
                            << ") => stime:" << pts_to_frac_frames_.subject_time()
                            << ", rtime:" << pts_to_frac_frames_.reference_time()
                            << ", sdelta:" << pts_to_frac_frames_.subject_delta()
                            << ", rdelta:" << pts_to_frac_frames_.reference_delta();
}

void BaseRenderer::AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) {
  TRACE_DURATION("audio", "BaseRenderer::AddPayloadBuffer");
  auto cleanup = fit::defer([this]() { context_.route_graph().RemoveRenderer(*this); });

  AUD_VLOG_OBJ(TRACE, this) << " (id: " << id << ")";

  // TODO(13655): Lift this restriction.
  if (IsOperating()) {
    FX_LOGS(ERROR) << "Attempted to set payload buffer while in operational mode.";
    return;
  }

  auto vmo_mapper = fbl::MakeRefCounted<RefCountedVmoMapper>();
  // Ideally we would reject this request if we already have a payload buffer with |id|, however
  // some clients currently rely on being able to update the payload buffer without first calling
  // |RemovePayloadBuffer|.
  payload_buffers_[id] = vmo_mapper;
  zx_status_t res = vmo_mapper->Map(payload_buffer, 0, 0, ZX_VM_PERM_READ, context_.vmar());
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Failed to map payload buffer";
    return;
  }

  REP(AddingRendererPayloadBuffer(*this, id, vmo_mapper->size()));

  // Things went well, cancel the cleanup hook. If our config had been validated previously, it will
  // have to be revalidated as we move into the operational phase of our life.
  InvalidateConfiguration();
  cleanup.cancel();
}

void BaseRenderer::RemovePayloadBuffer(uint32_t id) {
  TRACE_DURATION("audio", "BaseRenderer::RemovePayloadBuffer");
  auto cleanup = fit::defer([this]() { context_.route_graph().RemoveRenderer(*this); });

  AUD_VLOG_OBJ(TRACE, this) << " (id: " << id << ")";

  // TODO(13655): Lift this restriction.
  if (IsOperating()) {
    FX_LOGS(ERROR) << "Attempted to remove payload buffer while in the operational mode.";
    return;
  }

  if (payload_buffers_.erase(id) != 1) {
    FX_LOGS(ERROR) << "Invalid payload buffer id";
    return;
  }

  REP(RemovingRendererPayloadBuffer(*this, id));
  cleanup.cancel();
}

void BaseRenderer::SetPtsUnits(uint32_t tick_per_second_numerator,
                               uint32_t tick_per_second_denominator) {
  TRACE_DURATION("audio", "BaseRenderer::SetPtsUnits");
  auto cleanup = fit::defer([this]() { context_.route_graph().RemoveRenderer(*this); });

  AUD_VLOG_OBJ(TRACE, this) << " (pts ticks per sec: " << std::dec << tick_per_second_numerator
                            << " / " << tick_per_second_denominator << ")";

  if (IsOperating()) {
    FX_LOGS(ERROR) << "Attempted to set PTS units while in operational mode.";
    return;
  }

  if (!tick_per_second_numerator || !tick_per_second_denominator) {
    FX_LOGS(ERROR) << "Bad PTS ticks per second (" << tick_per_second_numerator << "/"
                   << tick_per_second_denominator << ")";
    return;
  }

  pts_ticks_per_second_ = TimelineRate(tick_per_second_numerator, tick_per_second_denominator);

  // Things went well, cancel the cleanup hook. If our config had been validated previously, it will
  // have to be revalidated as we move into the operational phase of our life.
  InvalidateConfiguration();
  cleanup.cancel();
}

void BaseRenderer::SetPtsContinuityThreshold(float threshold_seconds) {
  TRACE_DURATION("audio", "BaseRenderer::SetPtsContinuityThreshold");
  auto cleanup = fit::defer([this]() { context_.route_graph().RemoveRenderer(*this); });

  AUD_VLOG_OBJ(TRACE, this) << " (" << threshold_seconds << " sec)";

  if (IsOperating()) {
    FX_LOGS(ERROR) << "Attempted to set PTS cont threshold while in operational mode.";
    return;
  }

  if (threshold_seconds < 0.0) {
    FX_LOGS(ERROR) << "Invalid PTS continuity threshold (" << threshold_seconds << ")";
    return;
  }

  REP(SettingRendererPtsContinuityThreshold(*this, threshold_seconds));

  pts_continuity_threshold_ = threshold_seconds;
  pts_continuity_threshold_set_ = true;

  // Things went well, cancel the cleanup hook. If our config had been validated previously, it will
  // have to be revalidated as we move into the operational phase of our life.
  InvalidateConfiguration();
  cleanup.cancel();
}

void BaseRenderer::SetReferenceClock(zx::handle ref_clock) {
  TRACE_DURATION("audio", "BaseRenderer::SetReferenceClock");
  auto cleanup = fit::defer([this]() { context_.route_graph().RemoveRenderer(*this); });

  AUD_VLOG_OBJ(TRACE, this);

  if (IsOperating()) {
    FX_LOGS(ERROR) << "Attempted to set reference clock while in operational mode.";
    return;
  }

  FX_NOTIMPLEMENTED();
}

void BaseRenderer::SendPacket(fuchsia::media::StreamPacket packet, SendPacketCallback callback) {
  TRACE_DURATION("audio", "BaseRenderer::SendPacket");
  auto cleanup = fit::defer([this]() { context_.route_graph().RemoveRenderer(*this); });

  // It is an error to attempt to send a packet before we have established at least a minimum valid
  // configuration. IOW - the format must have been configured, and we must have an established
  // payload buffer.
  if (!ValidateConfig()) {
    FX_LOGS(ERROR) << "Failed to validate configuration during SendPacket";
    return;
  }

  // Lookup our payload buffer.
  auto it = payload_buffers_.find(packet.payload_buffer_id);
  if (it == payload_buffers_.end()) {
    FX_LOGS(ERROR) << "Invalid payload_buffer_id";
    return;
  }
  auto payload_buffer = it->second;

  // Start by making sure that the region we are receiving is made from an integral number of audio
  // frames. Count the total number of frames in the process.
  uint32_t frame_size = format()->bytes_per_frame();
  FX_DCHECK(frame_size != 0);
  if (packet.payload_size % frame_size) {
    FX_LOGS(ERROR) << "Region length (" << packet.payload_size
                   << ") is not divisible by by audio frame size (" << frame_size << ")";
    return;
  }

  // Make sure that we don't exceed the maximum permissible frames-per-packet.
  uint32_t frame_count = packet.payload_size / frame_size;
  if (frame_count > kMaxFrames) {
    FX_LOGS(ERROR) << "Audio frame count (" << frame_count << ") exceeds maximum allowed ("
                   << kMaxFrames << ")";
    return;
  }

  // Make sure that the packet offset/size exists entirely within the payload buffer.
  FX_DCHECK(payload_buffer != nullptr);
  uint64_t start = packet.payload_offset;
  uint64_t end = start + packet.payload_size;
  uint64_t pb_size = payload_buffer->size();
  if ((start >= pb_size) || (end > pb_size)) {
    FX_LOGS(ERROR) << "Bad packet range [" << start << ", " << end << "). Payload buffer size is "
                   << pb_size;
    return;
  }

  REP(SendingRendererPacket(*this, packet));

  // Compute the PTS values for this packet applying our interpolation and continuity thresholds as
  // we go. Start by checking to see if this our PTS to frames transformation needs to be computed
  // (this should be needed after startup, and after each flush operation).
  if (!pts_to_frac_frames_valid_) {
    ComputePtsToFracFrames((packet.pts == fuchsia::media::NO_TIMESTAMP) ? 0 : packet.pts);
  }

  // Now compute the starting PTS expressed in fractional input frames. If no explicit PTS was
  // provided, interpolate using the next expected PTS.
  FractionalFrames<int64_t> start_pts;
  FractionalFrames<int64_t> packet_ffpts{0};
  if (packet.pts == fuchsia::media::NO_TIMESTAMP) {
    start_pts = next_frac_frame_pts_;
  } else {
    // Looks like we have an explicit PTS on this packet. Boost it into the fractional input frame
    // domain, then apply our continuity threshold rules.
    packet_ffpts = FractionalFrames<int64_t>::FromRaw(pts_to_frac_frames_.Apply(packet.pts));
    FractionalFrames<int64_t> delta = packet_ffpts - next_frac_frame_pts_;
    delta = delta.Absolute();
    start_pts =
        (delta < pts_continuity_threshold_frac_frame_) ? next_frac_frame_pts_ : packet_ffpts;
  }

  uint32_t frame_offset = packet.payload_offset / frame_size;
  AUD_VLOG_OBJ(SPEW, this) << " [pkt " << std::hex << std::setw(8) << packet_ffpts.raw_value()
                           << ", now " << std::setw(8) << next_frac_frame_pts_.raw_value()
                           << "] => " << std::setw(8) << start_pts.raw_value() << " - "
                           << std::setw(8)
                           << start_pts.raw_value() + pts_to_frac_frames_.Apply(frame_count)
                           << ", offset " << std::setw(7)
                           << pts_to_frac_frames_.Apply(frame_offset);

  // Regardless of timing, capture this data to file.
  auto packet_buff = reinterpret_cast<uint8_t*>(payload_buffer->start()) + packet.payload_offset;
  wav_writer_.Write(packet_buff, packet.payload_size);
  wav_writer_.UpdateHeader();

  // Snap the starting pts to an input frame boundary.
  //
  // TODO(13374): Don't do this. If a user wants to write an explicit timestamp on a source packet
  // which schedules the packet to start at a fractional position on the source time line, we should
  // probably permit this. We need to make sure that the mixer cores are ready to handle this case
  // before proceeding, however.
  start_pts = FractionalFrames<int64_t>(start_pts.Floor());

  // Create the packet.
  auto packet_ref = packet_allocator_.New(
      payload_buffer, packet.payload_offset, FractionalFrames<uint32_t>(frame_count), start_pts,
      context_.threading_model().FidlDomain().dispatcher(), std::move(callback));
  if (!packet_ref) {
    FX_LOGS(ERROR) << "Client created too many concurrent Packets; Allocator has created "
                   << packet_allocator_.obj_count() << " / " << packet_allocator_.max_obj_count()
                   << " max allocations";
    return;
  }

  // The end pts is the value we will use for the next packet's start PTS, if the user does not
  // provide an explicit PTS.
  next_frac_frame_pts_ = packet_ref->end();

  // Distribute our packet to all our dest links
  for (auto& [_, packet_queue] : packet_queues_) {
    packet_queue->PushPacket(packet_ref);
  }

  // Things went well, cancel the cleanup hook.
  cleanup.cancel();
}

void BaseRenderer::SendPacketNoReply(fuchsia::media::StreamPacket packet) {
  TRACE_DURATION("audio", "BaseRenderer::SendPacketNoReply");
  AUD_VLOG_OBJ(SPEW, this);

  SendPacket(packet, nullptr);
}

void BaseRenderer::EndOfStream() {
  TRACE_DURATION("audio", "BaseRenderer::EndOfStream");
  AUD_VLOG_OBJ(TRACE, this);

  ReportStop();
  // Does nothing.
}

void BaseRenderer::DiscardAllPackets(DiscardAllPacketsCallback callback) {
  TRACE_DURATION("audio", "BaseRenderer::DiscardAllPackets");
  AUD_VLOG_OBJ(TRACE, this);

  // If the user has requested a callback, create the flush token we will use to invoke the callback
  // at the proper time.
  fbl::RefPtr<PendingFlushToken> flush_token;
  if (callback != nullptr) {
    flush_token = PendingFlushToken::Create(context_.threading_model().FidlDomain().dispatcher(),
                                            std::move(callback));
  }

  // Tell each link to flush. If link is currently processing pending data, it will take a reference
  // to the flush token and ensure a callback is queued at the proper time (after all pending
  // packet-complete callbacks are queued).
  for (auto& [_, packet_queue] : packet_queues_) {
    packet_queue->Flush(flush_token);
  }
}

void BaseRenderer::DiscardAllPacketsNoReply() {
  TRACE_DURATION("audio", "BaseRenderer::DiscardAllPacketsNoReply");
  AUD_VLOG_OBJ(TRACE, this);

  DiscardAllPackets(nullptr);
}

void BaseRenderer::Play(int64_t _reference_time, int64_t media_time, PlayCallback callback) {
  TRACE_DURATION("audio", "BaseRenderer::Play");
  AUD_VLOG_OBJ(TRACE, this)
      << " (ref: " << (_reference_time == fuchsia::media::NO_TIMESTAMP ? -1 : _reference_time)
      << ", media: " << (media_time == fuchsia::media::NO_TIMESTAMP ? -1 : media_time) << ")";
  zx::time reference_time(_reference_time);

  auto cleanup = fit::defer([this]() { context_.route_graph().RemoveRenderer(*this); });

  if (!ValidateConfig()) {
    FX_LOGS(ERROR) << "Failed to validate configuration during Play";
    return;
  }

  // TODO(mpuryear): What do we want to do here if we are already playing?

  // Did the user supply a reference time? If not, figure out a safe starting time based on the
  // outputs we are currently linked to.
  //
  // TODO(mpuryear): query the actual reference clock, don't assume CLOCK_MONO
  if (reference_time.get() == fuchsia::media::NO_TIMESTAMP) {
    // TODO(mpuryear): How much more than the minimum clock lead time do we want to pad this by?
    // Also, if/when lead time requirements change, do we want to introduce a discontinuity?
    //
    // We could consider an explicit mode (make it default) where timing across outputs is treated
    // as "loose". Specifically, make no effort to account for external latency, nor to synchronize
    // streams across multiple parallel outputs. In this mode we must update lead time upon changes
    // in internal interconnect requirements, but impact should be small since internal lead time
    // factors tend to be small, while external factors can be huge.
    reference_time = zx::clock::get_monotonic() + min_lead_time_ + kPaddingForUnspecifiedRefTime;
  }

  // If no media time was specified, use the first pending packet's media time.
  //
  // Note: users specify the units for media time by calling SetPtsUnits(), or nanoseconds if this
  // is never called. Internally we use fractional input frames, on the timeline defined when
  // transitioning to operational mode.
  FractionalFrames<int64_t> frac_frame_media_time;

  if (media_time == fuchsia::media::NO_TIMESTAMP) {
    // Are we resuming from pause?
    if (pause_time_frac_frames_valid_) {
      frac_frame_media_time = pause_time_frac_frames_;
    } else {
      // TODO(mpuryear): peek the first PTS of the pending queue.
      frac_frame_media_time = FractionalFrames<int64_t>(0);
    }

    // If we do not know the pts_to_frac_frames relationship yet, compute one.
    if (!pts_to_frac_frames_valid_) {
      next_frac_frame_pts_ = frac_frame_media_time;
      ComputePtsToFracFrames(0);
    }

    media_time = pts_to_frac_frames_.ApplyInverse(frac_frame_media_time.raw_value());
  } else {
    // If we do not know the pts_to_frac_frames relationship yet, compute one.
    if (!pts_to_frac_frames_valid_) {
      ComputePtsToFracFrames(media_time);
      frac_frame_media_time = next_frac_frame_pts_;
    } else {
      frac_frame_media_time =
          FractionalFrames<int64_t>::FromRaw(pts_to_frac_frames_.Apply(media_time));
    }
  }

  // Update our transformation.
  //
  // TODO(mpuryear): if we need to trigger a remix for our outputs, do it here.
  reference_clock_to_fractional_frames_->Update(TimelineFunction(
      frac_frame_media_time.raw_value(), reference_time.get(), frac_frames_per_ref_tick_));

  AUD_VLOG_OBJ(TRACE, this)
      << " Actual (ref: "
      << (reference_time.get() == fuchsia::media::NO_TIMESTAMP ? -1 : reference_time.get())
      << ", media: " << (media_time == fuchsia::media::NO_TIMESTAMP ? -1 : media_time) << ")";

  // If the user requested a callback, invoke it now.
  if (callback != nullptr) {
    callback(reference_time.get(), media_time);
  }

  ReportStart();
  // Things went well, cancel the cleanup hook.
  cleanup.cancel();
}

void BaseRenderer::PlayNoReply(int64_t reference_time, int64_t media_time) {
  TRACE_DURATION("audio", "BaseRenderer::PlayNoReply");
  AUD_VLOG_OBJ(TRACE, this)
      << " (ref: " << (reference_time == fuchsia::media::NO_TIMESTAMP ? -1 : reference_time)
      << ", media: " << (media_time == fuchsia::media::NO_TIMESTAMP ? -1 : media_time) << ")";

  if (reference_time != fuchsia::media::NO_TIMESTAMP) {
    reference_time += kPaddingForPlayNoReplyWithRefTime.to_nsecs();
  }

  Play(reference_time, media_time, nullptr);
}

void BaseRenderer::Pause(PauseCallback callback) {
  TRACE_DURATION("audio", "BaseRenderer::Pause");
  auto cleanup = fit::defer([this]() { context_.route_graph().RemoveRenderer(*this); });

  if (!ValidateConfig()) {
    FX_LOGS(ERROR) << "Failed to validate configuration during Pause";
    return;
  }

  // Update our reference clock to fractional frame transformation, keeping it 1st order continuous.
  int64_t ref_clock_now;

  // TODO(mpuryear): query the actual reference clock, don't assume CLOCK_MONO
  ref_clock_now = zx::clock::get_monotonic().get();
  pause_time_frac_frames_ = FractionalFrames<int64_t>::FromRaw(
      reference_clock_to_fractional_frames_->Apply(ref_clock_now));
  pause_time_frac_frames_valid_ = true;

  reference_clock_to_fractional_frames_->Update(
      TimelineFunction(pause_time_frac_frames_.raw_value(), ref_clock_now, {0, 1}));

  // If we do not know the pts_to_frac_frames relationship yet, compute one.
  if (!pts_to_frac_frames_valid_) {
    next_frac_frame_pts_ = pause_time_frac_frames_;
    ComputePtsToFracFrames(0);
  }

  // If the user requested a callback, figure out the media time that we paused at and report back.
  AUD_VLOG_OBJ(TRACE, this) << ". Actual (ref: " << ref_clock_now << ", media: "
                            << pts_to_frac_frames_.ApplyInverse(pause_time_frac_frames_.raw_value())
                            << ")";

  if (callback != nullptr) {
    int64_t paused_media_time =
        pts_to_frac_frames_.ApplyInverse(pause_time_frac_frames_.raw_value());
    callback(ref_clock_now, paused_media_time);
  }

  ReportStop();

  // Things went well, cancel the cleanup hook.
  cleanup.cancel();
}

void BaseRenderer::PauseNoReply() {
  TRACE_DURATION("audio", "BaseRenderer::PauseNoReply");
  AUD_VLOG_OBJ(TRACE, this);
  Pause(nullptr);
}

void BaseRenderer::OnLinkAdded() { RecomputeMinLeadTime(); }

void BaseRenderer::EnableMinLeadTimeEvents(bool enabled) {
  TRACE_DURATION("audio", "BaseRenderer::EnableMinLeadTimeEvents");
  AUD_VLOG_OBJ(TRACE, this);

  min_lead_time_events_enabled_ = enabled;
  if (enabled) {
    ReportNewMinLeadTime();
  }
}

void BaseRenderer::GetMinLeadTime(GetMinLeadTimeCallback callback) {
  TRACE_DURATION("audio", "BaseRenderer::GetMinLeadTime");
  AUD_VLOG_OBJ(TRACE, this);

  callback(min_lead_time_.to_nsecs());
}

void BaseRenderer::ReportNewMinLeadTime() {
  TRACE_DURATION("audio", "BaseRenderer::ReportNewMinLeadTime");
  if (min_lead_time_events_enabled_) {
    AUD_VLOG_OBJ(TRACE, this);

    auto& lead_time_event = audio_renderer_binding_.events();
    lead_time_event.OnMinLeadTimeChanged(min_lead_time_.to_nsecs());
  }
}

}  // namespace media::audio
