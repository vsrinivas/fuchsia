// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/audio_renderer_impl.h"

#include <lib/fit/defer.h>

#include "src/lib/fxl/arraysize.h"
#include "src/media/audio/audio_core/audio_core_impl.h"
#include "src/media/audio/audio_core/audio_output.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {

// If client does not specify a ref_time for Play, pad it by this amount
constexpr zx::duration kPaddingForUnspecifiedRefTime = zx::msec(20);

// Short-term workaround (until clients who timestamp are updated): if a client
// specifies ref_time but uses PlayNoReply (thus doesn't want the callback
// telling them what actual ref_time was), secretly pad by this amount.
constexpr zx::duration kPaddingForPlayNoReplyWithRefTime = zx::msec(10);

// To what extent should client-side underflows be logged? (A "client-side underflow" refers to when
// all or part of a packet's data is discarded because its start timestamp has already passed.)
// For each Renderer, we will log the first underflow. For subsequent occurrences, depending on
// audio_core's logging level, we throttle how frequently these are displayed. If log_level is set
// to TRACE or SPEW, all client-side underflows are logged -- at log_level -1: VLOG TRACE -- as
// specified by kRenderUnderflowTraceInterval. If set to INFO, we log less often, at log_level
// 1: INFO, throttling by the factor kRenderUnderflowInfoInterval. If set to WARNING or higher,
// we throttle these even more, specified by kRenderUnderflowErrorInterval. Note: by default we
// set NDEBUG builds to WARNING and DEBUG builds to INFO. To disable all logging of client-side
// underflows, set kLogRenderUnderflow to false.
static constexpr bool kLogRenderUnderflow = true;
static constexpr uint16_t kRenderUnderflowTraceInterval = 1;
static constexpr uint16_t kRenderUnderflowInfoInterval = 10;
static constexpr uint16_t kRenderUnderflowErrorInterval = 100;

fbl::RefPtr<AudioRendererImpl> AudioRendererImpl::Create(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request,
    async_dispatcher_t* dispatcher, RouteGraph* route_graph, AudioAdmin* admin,
    fbl::RefPtr<fzl::VmarManager> vmar, StreamVolumeManager* volume_manager) {
  return fbl::AdoptRef(new AudioRendererImpl(std::move(audio_renderer_request), dispatcher,
                                             route_graph, admin, vmar, volume_manager));
}

fbl::RefPtr<AudioRendererImpl> AudioRendererImpl::Create(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request,
    AudioCoreImpl* owner) {
  return Create(std::move(audio_renderer_request),
                owner->threading_model().FidlDomain().dispatcher(), &owner->route_graph(),
                &owner->audio_admin(), owner->vmar(), &owner->volume_manager());
}

AudioRendererImpl::AudioRendererImpl(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request,
    async_dispatcher_t* dispatcher, RouteGraph* route_graph, AudioAdmin* admin,
    fbl::RefPtr<fzl::VmarManager> vmar, StreamVolumeManager* volume_manager)
    : AudioObject(Type::AudioRenderer),
      usage_(fuchsia::media::AudioRenderUsage::MEDIA),
      dispatcher_(dispatcher),
      route_graph_(*route_graph),
      admin_(*admin),
      vmar_(std::move(vmar)),
      volume_manager_(*volume_manager),
      audio_renderer_binding_(this, std::move(audio_renderer_request)),
      pts_ticks_per_second_(1000000000, 1),
      reference_clock_to_fractional_frames_(fbl::MakeRefCounted<VersionedTimelineFunction>()),
      underflow_count_(0u),
      partial_underflow_count_(0u) {
  TRACE_DURATION("audio", "AudioRendererImpl::AudioRendererImpl");
  FX_DCHECK(admin);
  FX_DCHECK(route_graph);
  REP(AddingRenderer(*this));
  AUD_VLOG_OBJ(TRACE, this);

  std::vector<fuchsia::media::AudioRenderUsage> allowed_usages;
  allowed_usages.push_back(fuchsia::media::AudioRenderUsage::MEDIA);
  allowed_usages.push_back(fuchsia::media::AudioRenderUsage::BACKGROUND);
  allowed_usages.push_back(fuchsia::media::AudioRenderUsage::COMMUNICATION);
  allowed_usages.push_back(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  allowed_usages.push_back(fuchsia::media::AudioRenderUsage::INTERRUPTION);
  allowed_usages_ = std::move(allowed_usages);

  volume_manager_.AddStream(this);

  audio_renderer_binding_.set_error_handler(
      [this](zx_status_t status) { route_graph_.RemoveRenderer(this); });
}

AudioRendererImpl::~AudioRendererImpl() {
  AUD_VLOG_OBJ(TRACE, this);

  Shutdown();

  volume_manager_.RemoveStream(this);
  REP(RemovingRenderer(*this));
}

void AudioRendererImpl::ReportStart() { admin_.UpdateRendererState(usage_, true, this); }

void AudioRendererImpl::ReportStop() { admin_.UpdateRendererState(usage_, false, this); }

void AudioRendererImpl::Shutdown() {
  TRACE_DURATION("audio", "AudioRendererImpl::Shutdown");
  AUD_VLOG_OBJ(TRACE, this);

  ReportStop();

  wav_writer_.Close();
  gain_control_bindings_.CloseAll();
  payload_buffers_.clear();
}

zx_status_t AudioRendererImpl::InitializeDestLink(const fbl::RefPtr<AudioLink>& link) {
  TRACE_DURATION("audio", "AudioRendererImpl::InitializeDestLink");
  auto queue = fbl::MakeRefCounted<PacketQueue>(*format(), reference_clock_to_fractional_frames_);
  packet_queues_.insert({link.get(), queue});
  link->set_stream(std::move(queue));
  return ZX_OK;
}

void AudioRendererImpl::CleanupDestLink(const fbl::RefPtr<AudioLink>& link) {
  TRACE_DURATION("audio", "AudioRendererImpl::CleanupDestLink");
  auto it = packet_queues_.find(link.get());
  FX_CHECK(it != packet_queues_.end());
  packet_queues_.erase(it);
}

void AudioRendererImpl::RecomputeMinLeadTime() {
  TRACE_DURATION("audio", "AudioRendererImpl::RecomputeMinLeadTime");
  zx::duration cur_lead_time;

  ForEachDestLink([&cur_lead_time](auto& link) {
    if (link.GetDest()->is_output()) {
      const auto output = fbl::RefPtr<AudioDevice>::Downcast(link.GetDest());

      cur_lead_time = std::max(cur_lead_time, output->min_lead_time());
    }
  });

  if (min_lead_time_ != cur_lead_time) {
    REP(SettingRendererMinLeadTime(*this, cur_lead_time));
    min_lead_time_ = cur_lead_time;
    ReportNewMinLeadTime();
  }
}

void AudioRendererImpl::SetUsage(fuchsia::media::AudioRenderUsage usage) {
  TRACE_DURATION("audio", "AudioRendererImpl::SetUsage");
  if (usage == usage_) {
    return;
  }
  for (auto allowed : allowed_usages_) {
    if (allowed == usage) {
      ReportStop();
      usage_ = usage;
      volume_manager_.NotifyStreamChanged(this);
      route_graph_.SetRendererRoutingProfile(
          this, {.routable = format_valid(), .usage = GetStreamUsage()});
      if (IsOperating())
        ReportStart();
      return;
    }
  }
  FX_LOGS(ERROR) << "Disallowed or unknown usage - terminating the stream";
  route_graph_.RemoveRenderer(this);
}

// IsOperating is true any time we have any packets in flight. Configuration functions cannot be
// called any time we are operational.
bool AudioRendererImpl::IsOperating() {
  TRACE_DURATION("audio", "AudioRendererImpl::IsOperating");

  for (const auto& [_, packet_queue] : packet_queues_) {
    // If the packet queue is not empty then this link _is_ operating.
    if (!packet_queue->empty()) {
      return true;
    }
  }
  return false;
}

bool AudioRendererImpl::ValidateConfig() {
  TRACE_DURATION("audio", "AudioRendererImpl::ValidateConfig");
  if (config_validated_) {
    return true;
  }

  if (!format_valid() || payload_buffers_.empty()) {
    return false;
  }

  // Compute the number of fractional frames per PTS tick.
  uint32_t fps = format()->stream_type().frames_per_second;
  uint32_t frac_fps = fps << kPtsFractionalBits;
  frac_frames_per_pts_tick_ =
      TimelineRate::Product(pts_ticks_per_second_.Inverse(), TimelineRate(frac_fps, 1));

  // Compute the PTS continuity threshold expressed in fractional input frames.
  if (!pts_continuity_threshold_set_) {
    // The user has not explicitly set a continuity threshold. Default to 1/2
    // of a PTS tick expressed in fractional input frames, rounded up.
    pts_continuity_threshold_frac_frame_ = (frac_frames_per_pts_tick_.Scale(1) + 1) >> 1;
  } else {
    pts_continuity_threshold_frac_frame_ =
        static_cast<double>(frac_fps) * pts_continuity_threshold_;
  }

  AUD_VLOG_OBJ(TRACE, this) << " threshold_set_: " << pts_continuity_threshold_set_
                            << ", thres_frac_frame_: " << std::hex
                            << pts_continuity_threshold_frac_frame_;

  // Compute the number of fractional frames per reference clock tick.
  //
  // TODO(mpuryear): handle the case where the reference clock nominal rate is
  // something other than CLOCK_MONOTONIC
  frac_frames_per_ref_tick_ = TimelineRate(frac_fps, 1000000000u);

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

void AudioRendererImpl::ComputePtsToFracFrames(int64_t first_pts) {
  TRACE_DURATION("audio", "AudioRendererImpl::ComputePtsToFracFrames");
  // We should not be calling this, if transformation is already valid.
  FX_DCHECK(!pts_to_frac_frames_valid_);
  pts_to_frac_frames_ =
      TimelineFunction(next_frac_frame_pts_, first_pts, frac_frames_per_pts_tick_);
  pts_to_frac_frames_valid_ = true;

  AUD_VLOG_OBJ(TRACE, this) << " (" << first_pts
                            << ") => stime:" << pts_to_frac_frames_.subject_time()
                            << ", rtime:" << pts_to_frac_frames_.reference_time()
                            << ", sdelta:" << pts_to_frac_frames_.subject_delta()
                            << ", rdelta:" << pts_to_frac_frames_.reference_delta();
}

void AudioRendererImpl::UnderflowOccurred(int64_t frac_source_start, int64_t frac_source_mix_point,
                                          zx::duration underflow_duration) {
  TRACE_INSTANT("audio", "AudioRendererImpl::UnderflowOccurred", TRACE_SCOPE_PROCESS);
  uint16_t underflow_count = std::atomic_fetch_add<uint16_t>(&underflow_count_, 1u);

  if constexpr (kLogRenderUnderflow) {
    auto underflow_msec = static_cast<double>(underflow_duration.to_nsecs()) / ZX_MSEC(1);

    if ((kRenderUnderflowErrorInterval > 0) &&
        (underflow_count % kRenderUnderflowErrorInterval == 0)) {
      FX_LOGS(ERROR) << "RENDER UNDERFLOW #" << underflow_count + 1 << " (1/"
                     << kRenderUnderflowErrorInterval << "): source-start 0x" << std::hex
                     << frac_source_start << " missed mix-point 0x" << frac_source_mix_point
                     << " by " << std::setprecision(4) << underflow_msec << " ms";

    } else if ((kRenderUnderflowInfoInterval > 0) &&
               (underflow_count % kRenderUnderflowInfoInterval == 0)) {
      FX_LOGS(INFO) << "RENDER UNDERFLOW #" << underflow_count + 1 << " (1/"
                    << kRenderUnderflowErrorInterval << "): source-start 0x" << std::hex
                    << frac_source_start << " missed mix-point 0x" << frac_source_mix_point
                    << " by " << std::setprecision(4) << underflow_msec << " ms";

    } else if ((kRenderUnderflowTraceInterval > 0) &&
               (underflow_count % kRenderUnderflowTraceInterval == 0)) {
      FX_VLOGS(TRACE) << "RENDER UNDERFLOW #" << underflow_count + 1 << " (1/"
                      << kRenderUnderflowErrorInterval << "): source-start 0x" << std::hex
                      << frac_source_start << " missed mix-point 0x" << frac_source_mix_point
                      << " by " << std::setprecision(4) << underflow_msec << " ms";
    }
  }
}

void AudioRendererImpl::PartialUnderflowOccurred(int64_t frac_source_offset,
                                                 int64_t dest_mix_offset) {
  TRACE_INSTANT("audio", "AudioRendererImpl::PartialUnderflowOccurred", TRACE_SCOPE_PROCESS);

  // Shifts by less than four source frames do not necessarily indicate underflow. A shift of this
  // duration can be caused by the round-to-nearest-dest-frame step, when our rate-conversion ratio
  // is sufficiently large (it can be as large as 4:1).
  if (frac_source_offset >= (Mixer::FRAC_ONE << 2)) {
    auto partial_underflow_count = std::atomic_fetch_add<uint16_t>(&partial_underflow_count_, 1u);
    if constexpr (kLogRenderUnderflow) {
      if ((kRenderUnderflowErrorInterval > 0) &&
          (partial_underflow_count % kRenderUnderflowErrorInterval == 0)) {
        FX_LOGS(WARNING) << "RENDER SHIFT #" << partial_underflow_count + 1 << " (1/"
                         << kRenderUnderflowErrorInterval << "): shifted by "
                         << (frac_source_offset < 0 ? "-0x" : "0x") << std::hex
                         << abs(frac_source_offset) << " source subframes and " << std::dec
                         << dest_mix_offset << " mix (output) frames";
      } else if ((kRenderUnderflowInfoInterval > 0) &&
                 (partial_underflow_count % kRenderUnderflowInfoInterval == 0)) {
        FX_LOGS(INFO) << "RENDER SHIFT #" << partial_underflow_count + 1 << " (1/"
                      << kRenderUnderflowInfoInterval << "): shifted by "
                      << (frac_source_offset < 0 ? "-0x" : "0x") << std::hex
                      << abs(frac_source_offset) << " source subframes and " << std::dec
                      << dest_mix_offset << " mix (output) frames";
      } else if ((kRenderUnderflowTraceInterval > 0) &&
                 (partial_underflow_count % kRenderUnderflowTraceInterval == 0)) {
        FX_VLOGS(TRACE) << "RENDER SHIFT #" << partial_underflow_count + 1 << " (1/"
                        << kRenderUnderflowTraceInterval << "): shifted by "
                        << (frac_source_offset < 0 ? "-0x" : "0x") << std::hex
                        << abs(frac_source_offset) << " source subframes and " << std::dec
                        << dest_mix_offset << " mix (output) frames";
      }
    }
  } else {
    if constexpr (kLogRenderUnderflow) {
      FX_VLOGS(TRACE) << "shifted " << dest_mix_offset
                      << " mix (output) frames to align with source packet";
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
//
// AudioRenderer Interface
//
void AudioRendererImpl::SetPcmStreamType(fuchsia::media::AudioStreamType format) {
  TRACE_DURATION("audio", "AudioRendererImpl::SetPcmStreamType");
  auto cleanup = fit::defer([this]() { route_graph_.RemoveRenderer(this); });

  AUD_VLOG_OBJ(TRACE, this);

  // We cannot change the format while we are currently operational
  if (IsOperating()) {
    FX_LOGS(ERROR) << "Attempted to set format while in operational mode.";
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
      FX_LOGS(ERROR) << "Unsupported sample format (" << fidl::ToUnderlying(format.sample_format)
                     << ") in fuchsia::media::AudioRendererImpl::SetPcmStreamType.";
      return;
  }

  if ((format.channels < fuchsia::media::MIN_PCM_CHANNEL_COUNT) ||
      (format.channels > fuchsia::media::MAX_PCM_CHANNEL_COUNT)) {
    FX_LOGS(ERROR)
        << "Invalid channel count (" << format.channels
        << ") in fuchsia::media::AudioRendererImpl::SetPcmStreamType. Must be in the range ["
        << fuchsia::media::MIN_PCM_CHANNEL_COUNT << ", " << fuchsia::media::MAX_PCM_CHANNEL_COUNT
        << "]";
    return;
  }

  if ((format.frames_per_second < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) ||
      (format.frames_per_second > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)) {
    FX_LOGS(ERROR)
        << "Invalid frame rate (" << format.frames_per_second
        << ") in fuchsia::media::AudioRendererImpl::SetPcmStreamType. Must be in the range ["
        << fuchsia::media::MIN_PCM_FRAMES_PER_SECOND << ", "
        << fuchsia::media::MAX_PCM_FRAMES_PER_SECOND << "]";
    return;
  }

  REP(SettingRendererStreamType(*this, format));

  // Create a new format info object so we can create links to outputs.
  fuchsia::media::AudioStreamType cfg;
  cfg.sample_format = format.sample_format;
  cfg.channels = format.channels;
  cfg.frames_per_second = format.frames_per_second;
  format_ = Format::Create(cfg);

  route_graph_.SetRendererRoutingProfile(this, {.routable = true, .usage = GetStreamUsage()});
  volume_manager_.NotifyStreamChanged(this);

  // Things went well, cancel the cleanup hook. If our config had been validated previously, it will
  // have to be revalidated as we move into the operational phase of our life.
  config_validated_ = false;
  cleanup.cancel();
}

void AudioRendererImpl::AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) {
  TRACE_DURATION("audio", "AudioRendererImpl::AddPayloadBuffer");
  auto cleanup = fit::defer([this]() { route_graph_.RemoveRenderer(this); });

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
  zx_status_t res = vmo_mapper->Map(payload_buffer, 0, 0, ZX_VM_PERM_READ, vmar_);
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Failed to map payload buffer";
    return;
  }

  REP(AddingRendererPayloadBuffer(*this, id, vmo_mapper->size()));

  // Things went well, cancel the cleanup hook. If our config had been validated previously, it will
  // have to be revalidated as we move into the operational phase of our life.
  config_validated_ = false;
  cleanup.cancel();
}

void AudioRendererImpl::RemovePayloadBuffer(uint32_t id) {
  TRACE_DURATION("audio", "AudioRendererImpl::RemovePayloadBuffer");
  auto cleanup = fit::defer([this]() { route_graph_.RemoveRenderer(this); });

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

void AudioRendererImpl::SetPtsUnits(uint32_t tick_per_second_numerator,
                                    uint32_t tick_per_second_denominator) {
  TRACE_DURATION("audio", "AudioRendererImpl::SetPtsUnits");
  auto cleanup = fit::defer([this]() { route_graph_.RemoveRenderer(this); });

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
  config_validated_ = false;
  cleanup.cancel();
}

void AudioRendererImpl::SetPtsContinuityThreshold(float threshold_seconds) {
  TRACE_DURATION("audio", "AudioRendererImpl::SetPtsContinuityThreshold");
  auto cleanup = fit::defer([this]() { route_graph_.RemoveRenderer(this); });

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
  config_validated_ = false;
  cleanup.cancel();
}

void AudioRendererImpl::SetReferenceClock(zx::handle ref_clock) {
  TRACE_DURATION("audio", "AudioRendererImpl::SetReferenceClock");
  auto cleanup = fit::defer([this]() { route_graph_.RemoveRenderer(this); });

  AUD_VLOG_OBJ(TRACE, this);

  if (IsOperating()) {
    FX_LOGS(ERROR) << "Attempted to set reference clock while in operational mode.";
    return;
  }

  FX_NOTIMPLEMENTED();
}

void AudioRendererImpl::SendPacket(fuchsia::media::StreamPacket packet,
                                   SendPacketCallback callback) {
  TRACE_DURATION("audio", "AudioRendererImpl::SendPacket");
  auto cleanup = fit::defer([this]() { route_graph_.RemoveRenderer(this); });

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
  static constexpr uint32_t kMaxFrames = std::numeric_limits<uint32_t>::max() >> kPtsFractionalBits;
  uint32_t frame_count = packet.payload_size / frame_size;
  if (frame_count > kMaxFrames) {
    FX_LOGS(ERROR) << "Audio frame count (" << frame_count << ") exceeds maximum allowed ("
                   << kMaxFrames << ")";
    return;
  }

  // Make sure that the packet offset/size exists entirely within the payload buffer.
  FX_DCHECK(payload_buffer != nullptr);
  uint64_t start = packet.payload_offset;
  uint32_t frame_offset = packet.payload_offset / frame_size;
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
  int64_t start_pts;
  int64_t packet_ffpts;
  if (packet.pts == fuchsia::media::NO_TIMESTAMP) {
    packet_ffpts = fuchsia::media::NO_TIMESTAMP;
    start_pts = next_frac_frame_pts_;
  } else {
    // Looks like we have an explicit PTS on this packet. Boost it into the fractional input frame
    // domain, then apply our continuity threshold rules.
    packet_ffpts = pts_to_frac_frames_.Apply(packet.pts);
    int64_t delta = std::abs(packet_ffpts - next_frac_frame_pts_);
    start_pts =
        (delta < pts_continuity_threshold_frac_frame_) ? next_frac_frame_pts_ : packet_ffpts;
  }
  AUD_VLOG_OBJ(SPEW, this) << " [pkt " << std::hex << std::setw(8) << packet_ffpts << ", now "
                           << std::setw(8) << next_frac_frame_pts_ << "] => " << std::setw(8)
                           << start_pts << " - " << std::setw(8)
                           << start_pts + pts_to_frac_frames_.Apply(frame_count) << ", offset "
                           << std::setw(7) << pts_to_frac_frames_.Apply(frame_offset);

  // Regardless of timing, capture this data to file.
  auto packet_buff = reinterpret_cast<uint8_t*>(payload_buffer->start()) + packet.payload_offset;
  wav_writer_.Write(packet_buff, packet.payload_size);
  wav_writer_.UpdateHeader();

  // Snap the starting pts to an input frame boundary.
  //
  // TODO(13374): Don't do this. If a user wants to write an explicit timestamp on an input
  // packet which schedules the packet to start at a fractional position on the input time line, we
  // should probably permit this. We need to make sure that the mixer cores are ready to handle this
  // case before proceeding, however.
  constexpr auto mask = ~((static_cast<int64_t>(1) << kPtsFractionalBits) - 1);
  start_pts &= mask;

  // Create the packet.
  auto packet_ref =
      fbl::MakeRefCounted<Packet>(payload_buffer, dispatcher_, std::move(callback), packet,
                                  frame_count << kPtsFractionalBits, start_pts);

  // The end pts is the value we will use for the next packet's start PTS, if the user does not
  // provide an explicit PTS.
  next_frac_frame_pts_ = packet_ref->end_pts();

  // Distribute our packet to all our dest links
  for (auto& [_, packet_queue] : packet_queues_) {
    packet_queue->PushPacket(packet_ref);
  }

  // Things went well, cancel the cleanup hook.
  cleanup.cancel();
}

void AudioRendererImpl::SendPacketNoReply(fuchsia::media::StreamPacket packet) {
  TRACE_DURATION("audio", "AudioRendererImpl::SendPacketNoReply");
  AUD_VLOG_OBJ(SPEW, this);

  SendPacket(packet, nullptr);
}

void AudioRendererImpl::EndOfStream() {
  TRACE_DURATION("audio", "AudioRendererImpl::EndOfStream");
  AUD_VLOG_OBJ(TRACE, this);

  ReportStop();
  // Does nothing.
}

void AudioRendererImpl::DiscardAllPackets(DiscardAllPacketsCallback callback) {
  TRACE_DURATION("audio", "AudioRendererImpl::DiscardAllPackets");
  AUD_VLOG_OBJ(TRACE, this);

  // If the user has requested a callback, create the flush token we will use to invoke the callback
  // at the proper time.
  fbl::RefPtr<PendingFlushToken> flush_token;
  if (callback != nullptr) {
    flush_token = PendingFlushToken::Create(dispatcher_, std::move(callback));
  }

  // Tell each link to flush. If link is currently processing pending data, it will take a reference
  // to the flush token and ensure a callback is queued at the proper time (after all pending
  // packet-complete callbacks are queued).
  for (auto& [_, packet_queue] : packet_queues_) {
    packet_queue->Flush(flush_token);
  }

  // Invalidate any internal state which gets reset after a flush. We set next_frac_frame_pts_
  // (ref_time specified in fractional PTS subframes, corresponding to when the next packet should
  // play) to be NOW plus min_lead_time plus safety factor, then define PTS 0 as that value (because
  // PTS is reset to 0 upon DiscardAllPackets, unless we are Paused).
  pts_to_frac_frames_valid_ = false;
  // TODO(mpuryear): query the actual reference clock, don't assume CLOCK_MONO
  auto ref_time_for_reset =
      zx::clock::get_monotonic() + min_lead_time_ + kPaddingForUnspecifiedRefTime;
  next_frac_frame_pts_ = reference_clock_to_fractional_frames_->Apply(ref_time_for_reset.get());
  ComputePtsToFracFrames(0);

  // TODO(mpuryear): Validate Pause => DiscardAll => Play(..., NO_TIMESTAMP) -- specifically that we
  // resume at exactly the paused media time.
  pause_time_frac_frames_valid_ = false;
  ReportStop();
}

void AudioRendererImpl::DiscardAllPacketsNoReply() {
  TRACE_DURATION("audio", "AudioRendererImpl::DiscardAllPacketsNoReply");
  AUD_VLOG_OBJ(TRACE, this);

  DiscardAllPackets(nullptr);
}

void AudioRendererImpl::Play(int64_t _reference_time, int64_t media_time, PlayCallback callback) {
  TRACE_DURATION("audio", "AudioRendererImpl::Play");
  AUD_VLOG_OBJ(TRACE, this)
      << " (ref: " << (_reference_time == fuchsia::media::NO_TIMESTAMP ? -1 : _reference_time)
      << ", media: " << (media_time == fuchsia::media::NO_TIMESTAMP ? -1 : media_time) << ")";
  zx::time reference_time(_reference_time);

  auto cleanup = fit::defer([this]() { route_graph_.RemoveRenderer(this); });

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
  int64_t frac_frame_media_time;

  if (media_time == fuchsia::media::NO_TIMESTAMP) {
    // Are we resuming from pause?
    if (pause_time_frac_frames_valid_) {
      frac_frame_media_time = pause_time_frac_frames_;
    } else {
      // TODO(mpuryear): peek the first PTS of the pending queue.
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
  // TODO(mpuryear): if we need to trigger a remix for our outputs, do it here.
  reference_clock_to_fractional_frames_->Update(
      TimelineFunction(frac_frame_media_time, reference_time.get(), frac_frames_per_ref_tick_));

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

void AudioRendererImpl::PlayNoReply(int64_t reference_time, int64_t media_time) {
  TRACE_DURATION("audio", "AudioRendererImpl::PlayNoReply");
  AUD_VLOG_OBJ(TRACE, this)
      << " (ref: " << (reference_time == fuchsia::media::NO_TIMESTAMP ? -1 : reference_time)
      << ", media: " << (media_time == fuchsia::media::NO_TIMESTAMP ? -1 : media_time) << ")";

  if (reference_time != fuchsia::media::NO_TIMESTAMP) {
    reference_time += kPaddingForPlayNoReplyWithRefTime.to_nsecs();
  }

  Play(reference_time, media_time, nullptr);
}

void AudioRendererImpl::Pause(PauseCallback callback) {
  TRACE_DURATION("audio", "AudioRendererImpl::Pause");
  auto cleanup = fit::defer([this]() { route_graph_.RemoveRenderer(this); });

  if (!ValidateConfig()) {
    FX_LOGS(ERROR) << "Failed to validate configuration during Pause";
    return;
  }

  // Update our reference clock to fractional frame transformation, keeping it 1st order continuous.
  int64_t ref_clock_now;

  // TODO(mpuryear): query the actual reference clock, don't assume CLOCK_MONO
  ref_clock_now = zx::clock::get_monotonic().get();
  pause_time_frac_frames_ = reference_clock_to_fractional_frames_->Apply(ref_clock_now);
  pause_time_frac_frames_valid_ = true;

  reference_clock_to_fractional_frames_->Update(
      TimelineFunction(pause_time_frac_frames_, ref_clock_now, {0, 1}));

  // If we do not know the pts_to_frac_frames relationship yet, compute one.
  if (!pts_to_frac_frames_valid_) {
    next_frac_frame_pts_ = pause_time_frac_frames_;
    ComputePtsToFracFrames(0);
  }

  // If the user requested a callback, figure out the media time that we paused at and report back.
  int64_t paused_media_time = pts_to_frac_frames_.ApplyInverse(pause_time_frac_frames_);

  AUD_VLOG_OBJ(TRACE, this) << ". Actual (ref: " << ref_clock_now
                            << ", media: " << paused_media_time << ")";

  if (callback != nullptr) {
    int64_t paused_media_time = pts_to_frac_frames_.ApplyInverse(pause_time_frac_frames_);
    callback(ref_clock_now, paused_media_time);
  }

  // Things went well, cancel the cleanup hook.
  cleanup.cancel();
}

void AudioRendererImpl::PauseNoReply() {
  TRACE_DURATION("audio", "AudioRendererImpl::PauseNoReply");
  AUD_VLOG_OBJ(TRACE, this);
  Pause(nullptr);
}

void AudioRendererImpl::OnLinkAdded() {
  volume_manager_.NotifyStreamChanged(this);
  RecomputeMinLeadTime();
}

bool AudioRendererImpl::GetStreamMute() const { return mute_; }

fuchsia::media::Usage AudioRendererImpl::GetStreamUsage() const {
  fuchsia::media::Usage usage;
  usage.set_render_usage(usage_);
  return usage;
}

void AudioRendererImpl::RealizeVolume(VolumeCommand volume_command) {
  ForEachDestLink([stream_gain_db = stream_gain_db_, &volume_command](auto& link) {
    float gain_db = link.volume_curve().VolumeToDb(volume_command.volume);

    gain_db = Gain::CombineGains(gain_db, stream_gain_db);
    gain_db = Gain::CombineGains(gain_db, volume_command.gain_db_adjustment);

    if (volume_command.ramp.has_value()) {
      link.gain().SetSourceGainWithRamp(gain_db, volume_command.ramp->duration,
                                        volume_command.ramp->ramp_type);
    } else {
      link.gain().SetSourceGain(gain_db);
    }
  });
}

// Set the stream gain, in each Renderer -> Output audio path. The Gain object contains multiple
// stages. In playback, renderer gain is pre-mix and hence is "source" gain; the Output device (or
// master) gain is "dest" gain.
void AudioRendererImpl::SetGain(float gain_db) {
  TRACE_DURATION("audio", "AudioRendererImpl::SetGain");
  AUD_VLOG_OBJ(TRACE, this) << " (" << gain_db << " dB)";

  // Anywhere we set stream_gain_db_, we should perform this range check.
  if (gain_db > fuchsia::media::audio::MAX_GAIN_DB ||
      gain_db < fuchsia::media::audio::MUTED_GAIN_DB || isnan(gain_db)) {
    FX_LOGS(ERROR) << "SetGain(" << gain_db << " dB) out of range.";
    route_graph_.RemoveRenderer(this);
    return;
  }

  if (stream_gain_db_ == gain_db) {
    return;
  }

  REP(SettingRendererGain(*this, gain_db));

  stream_gain_db_ = gain_db;
  volume_manager_.NotifyStreamChanged(this);

  NotifyGainMuteChanged();
}

// Set a stream gain ramp, in each Renderer -> Output audio path. Renderer gain is pre-mix and hence
// is the Source component in the Gain object.
void AudioRendererImpl::SetGainWithRamp(float gain_db, int64_t duration_ns,
                                        fuchsia::media::audio::RampType ramp_type) {
  TRACE_DURATION("audio", "AudioRendererImpl::SetGainWithRamp");
  zx::duration duration = zx::nsec(duration_ns);
  AUD_VLOG_OBJ(TRACE, this) << " (" << gain_db << " dB, " << duration.to_usecs() << " usec)";

  if (gain_db > fuchsia::media::audio::MAX_GAIN_DB ||
      gain_db < fuchsia::media::audio::MUTED_GAIN_DB || isnan(gain_db)) {
    FX_LOGS(ERROR) << "SetGainWithRamp(" << gain_db << " dB) out of range.";
    route_graph_.RemoveRenderer(this);
    return;
  }

  REP(SettingRendererGainWithRamp(*this, gain_db, duration, ramp_type));

  volume_manager_.NotifyStreamChanged(this, Ramp{duration, ramp_type});

  // TODO(mpuryear): implement GainControl notifications for gain ramps.
}

// Set a stream mute, in each Renderer -> Output audio path. For now, mute is handled by setting
// gain to a value guaranteed to be silent, but going forward we may pass this thru to the Gain
// object. Renderer gain/mute is pre-mix and hence is the Source component in the Gain object.
void AudioRendererImpl::SetMute(bool mute) {
  TRACE_DURATION("audio", "AudioRendererImpl::SetMute");
  // Only do the work if the request represents a change in state.
  if (mute_ == mute) {
    return;
  }
  AUD_VLOG_OBJ(TRACE, this) << " (mute: " << mute << ")";

  REP(SettingRendererMute(*this, mute));
  mute_ = mute;

  volume_manager_.NotifyStreamChanged(this);
  NotifyGainMuteChanged();
}

void AudioRendererImpl::BindGainControl(
    fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) {
  TRACE_DURATION("audio", "AudioRendererImpl::BindGainControl");
  AUD_VLOG_OBJ(TRACE, this);

  gain_control_bindings_.AddBinding(GainControlBinding::Create(this), std::move(request));
}

void AudioRendererImpl::EnableMinLeadTimeEvents(bool enabled) {
  TRACE_DURATION("audio", "AudioRendererImpl::EnableMinLeadTimeEvents");
  AUD_VLOG_OBJ(TRACE, this);

  min_lead_time_events_enabled_ = enabled;
  if (enabled) {
    ReportNewMinLeadTime();
  }
}

void AudioRendererImpl::GetMinLeadTime(GetMinLeadTimeCallback callback) {
  TRACE_DURATION("audio", "AudioRendererImpl::GetMinLeadTime");
  AUD_VLOG_OBJ(TRACE, this);

  callback(min_lead_time_.to_nsecs());
}

void AudioRendererImpl::ReportNewMinLeadTime() {
  TRACE_DURATION("audio", "AudioRendererImpl::ReportNewMinLeadTime");
  if (min_lead_time_events_enabled_) {
    AUD_VLOG_OBJ(TRACE, this);

    auto& lead_time_event = audio_renderer_binding_.events();
    lead_time_event.OnMinLeadTimeChanged(min_lead_time_.to_nsecs());
  }
}

void AudioRendererImpl::NotifyGainMuteChanged() {
  TRACE_DURATION("audio", "AudioRendererImpl::NotifyGainMuteChanged");
  // TODO(mpuryear): consider whether GainControl events should be disable-able, like MinLeadTime.
  AUD_VLOG_OBJ(TRACE, this) << " (" << stream_gain_db_ << " dB, mute: " << mute_ << ")";

  for (auto& gain_binding : gain_control_bindings_.bindings()) {
    gain_binding->events().OnGainMuteChanged(stream_gain_db_, mute_);
  }
}

void AudioRendererImpl::GainControlBinding::SetGain(float gain_db) {
  TRACE_DURATION("audio", "AudioRendererImpl::SetGain");
  owner_->SetGain(gain_db);
}

void AudioRendererImpl::GainControlBinding::SetGainWithRamp(
    float gain_db, int64_t duration_ns, fuchsia::media::audio::RampType ramp_type) {
  TRACE_DURATION("audio", "AudioRendererImpl::SetSourceGainWithRamp");
  owner_->SetGainWithRamp(gain_db, duration_ns, ramp_type);
}

void AudioRendererImpl::GainControlBinding::SetMute(bool mute) {
  TRACE_DURATION("audio", "AudioRendererImpl::SetMute");
  owner_->SetMute(mute);
}

}  // namespace media::audio
