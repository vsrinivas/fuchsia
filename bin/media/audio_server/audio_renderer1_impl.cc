// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_renderer1_impl.h"

#include <algorithm>
#include <limits>

#include "garnet/bin/media/audio_server/audio_device_manager.h"
#include "garnet/bin/media/audio_server/audio_renderer_format_info.h"
#include "garnet/bin/media/audio_server/audio_server_impl.h"
#include "garnet/bin/media/audio_server/constants.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media {
namespace audio {

// TODO(johngro): If there is ever a better way to do this type of static-table
// initialization using fidl generated structs, we should switch to it.
static const struct {
  fuchsia::media::AudioSampleFormat sample_format;
  uint32_t min_channels;
  uint32_t max_channels;
  uint32_t min_frames_per_second;
  uint32_t max_frames_per_second;
} kSupportedAudioTypeSets[] = {
    {
        .sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8,
        .min_channels = fuchsia::media::kMinLpcmChannelCount,
        .max_channels = fuchsia::media::kMaxLpcmChannelCount,
        .min_frames_per_second = fuchsia::media::kMinLpcmFramesPerSecond,
        .max_frames_per_second = fuchsia::media::kMaxLpcmFramesPerSecond,
    },
    {
        .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
        .min_channels = fuchsia::media::kMinLpcmChannelCount,
        .max_channels = fuchsia::media::kMaxLpcmChannelCount,
        .min_frames_per_second = fuchsia::media::kMinLpcmFramesPerSecond,
        .max_frames_per_second = fuchsia::media::kMaxLpcmFramesPerSecond,
    },
    {
        .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
        .min_channels = fuchsia::media::kMinLpcmChannelCount,
        .max_channels = fuchsia::media::kMaxLpcmChannelCount,
        .min_frames_per_second = fuchsia::media::kMinLpcmFramesPerSecond,
        .max_frames_per_second = fuchsia::media::kMaxLpcmFramesPerSecond,
    },
};

AudioRenderer1Impl::AudioRenderer1Impl(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer>
        audio_renderer_request,
    fidl::InterfaceRequest<fuchsia::media::MediaRenderer>
        media_renderer_request,
    AudioServerImpl* owner)
    : owner_(owner),
      audio_renderer_binding_(this, std::move(audio_renderer_request)),
      media_renderer_binding_(this, std::move(media_renderer_request)),
      pipe_(this, owner) {
  FXL_CHECK(nullptr != owner_);

  audio_renderer_binding_.set_error_handler([this]() -> void {
    audio_renderer_binding_.set_error_handler(nullptr);
    audio_renderer_binding_.Unbind();

    // If the media_renderer binding has also been closed, it is time to shut
    // down.
    if (!media_renderer_binding_.is_bound()) {
      Shutdown();
    }
  });

  media_renderer_binding_.set_error_handler([this]() -> void {
    media_renderer_binding_.set_error_handler(nullptr);
    media_renderer_binding_.Unbind();

    // If the audio_renderer binding has also been closed, it is time to shut
    // down.
    if (!audio_renderer_binding_.is_bound()) {
      Shutdown();
    }
  });

  timeline_control_point_.SetProgramRangeSetCallback(
      [this](uint64_t program, int64_t min_pts, int64_t max_pts) {
        pipe_.ProgramRangeSet(program, min_pts, max_pts);
      });

  timeline_control_point_.SetPrimeRequestedCallback(
      [this](TimelineControlPoint::PrimeCallback callback) {
        pipe_.PrimeRequested(std::move(callback));
      });
}

AudioRenderer1Impl::~AudioRenderer1Impl() {
  // assert that we have been cleanly shutdown already.
  FXL_DCHECK(!audio_renderer_binding_.is_bound());
  FXL_DCHECK(!media_renderer_binding_.is_bound());
}

fbl::RefPtr<AudioRenderer1Impl> AudioRenderer1Impl::Create(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer>
        audio_renderer_request,
    fidl::InterfaceRequest<fuchsia::media::MediaRenderer>
        media_renderer_request,
    AudioServerImpl* owner) {
  return fbl::AdoptRef(new AudioRenderer1Impl(std::move(audio_renderer_request),
                                              std::move(media_renderer_request),
                                              owner));
}

void AudioRenderer1Impl::Shutdown() {
  // If we have already been shutdown, then we are just waiting for the service
  // to destroy us.  Run some FXL_DCHECK sanity checks and get out.
  if (is_shutdown_) {
    FXL_DCHECK(!audio_renderer_binding_.is_bound());
    FXL_DCHECK(!media_renderer_binding_.is_bound());
    FXL_DCHECK(!pipe_.is_bound());
    FXL_DCHECK(!timeline_control_point_.is_bound());
    return;
  }

  is_shutdown_ = true;

  PreventNewLinks();
  Unlink();

  if (audio_renderer_binding_.is_bound()) {
    audio_renderer_binding_.set_error_handler(nullptr);
    audio_renderer_binding_.Unbind();
  }

  if (media_renderer_binding_.is_bound()) {
    media_renderer_binding_.set_error_handler(nullptr);
    media_renderer_binding_.Unbind();
  }

  // reset all of our internal state and close any other client connections in
  // the process.
  pipe_.Reset();
  timeline_control_point_.Reset();
  throttle_output_link_ = nullptr;

  FXL_DCHECK(owner_);
  if (InContainer()) {
    owner_->GetDeviceManager().RemoveRenderer(this);
  }
}

void AudioRenderer1Impl::GetSupportedMediaTypes(
    GetSupportedMediaTypesCallback cbk) {
  cbk(SupportedMediaTypes());
}

void AudioRenderer1Impl::SetMediaType(fuchsia::media::MediaType media_type) {
  // Check the requested configuration.
  if ((media_type.medium != fuchsia::media::MediaTypeMedium::AUDIO) ||
      (media_type.encoding != fuchsia::media::kAudioEncodingLpcm) ||
      (!media_type.details.is_audio())) {
    FXL_LOG(ERROR) << "Unsupported configuration requested in "
                   << "fuchsia::media::AudioRenderer::SetMediaType.  "
                   << "Media type must be LPCM audio.";
    Shutdown();
    return;
  }

  // Search our supported configuration sets to find one compatible with this
  // request.
  auto& cfg = media_type.details.audio();
  size_t i;
  for (i = 0; i < arraysize(kSupportedAudioTypeSets); ++i) {
    const auto& cfg_set = kSupportedAudioTypeSets[i];

    if ((cfg.sample_format == cfg_set.sample_format) &&
        (cfg.channels >= cfg_set.min_channels) &&
        (cfg.channels <= cfg_set.max_channels) &&
        (cfg.frames_per_second >= cfg_set.min_frames_per_second) &&
        (cfg.frames_per_second <= cfg_set.max_frames_per_second)) {
      break;
    }
  }

  if (i >= arraysize(kSupportedAudioTypeSets)) {
    FXL_LOG(ERROR) << "Unsupported LPCM configuration requested in "
                   << "fuchsia::media::AudioRenderer::SetMediaType.  "
                   << "(format = " << cfg.sample_format
                   << ", channels = " << static_cast<uint32_t>(cfg.channels)
                   << ", frames_per_second = " << cfg.frames_per_second << ")";
    Shutdown();
    return;
  }

  bool has_pending =
      (throttle_output_link_ && !throttle_output_link_->pending_queue_empty());
  if (!has_pending) {
    fbl::AutoLock links_lock(&links_lock_);
    // Renderers should never be linked to sources.
    FXL_DCHECK(source_links_.empty());

    for (const auto& link : dest_links_) {
      FXL_DCHECK(link->source_type() == AudioLink::SourceType::Packet);
      auto packet_link = static_cast<AudioLinkPacketSource*>(link.get());
      if (!packet_link->pending_queue_empty()) {
        has_pending = true;
        break;
      }
    }
  }

  if (has_pending) {
    FXL_LOG(ERROR) << "Attempted to set format with audio still pending!";
    Shutdown();
    return;
  }

  // Everything checks out.  Discard any existing links we hold (including
  // throttle output).  New links need to be created with our new format.
  Unlink();
  throttle_output_link_ = nullptr;

  pipe_.SetPtsRate(TimelineRate(cfg.frames_per_second, 1));

  // Create a new format info object so we can create links to outputs.
  format_info_ = AudioRendererFormatInfo::Create(std::move(cfg));

  // Have the audio output manager initialize our set of outputs.  Note; there
  // is currently no need for a lock here.  Methods called from our user-facing
  // interfaces are serialized by nature of the fidl framework, and none of the
  // output manager's threads should ever need to manipulate the set.  Cleanup
  // of outputs which have gone away is currently handled in a lazy fashion when
  // the renderer fails to promote its weak reference during an operation
  // involving its outputs.
  //
  // TODO(johngro): someday, we will need to deal with recalculating properties
  // which depend on a renderer's current set of outputs (for example, the
  // minimum latency).  This will probably be done using a dirty flag in the
  // renderer implementations, and scheduling a job to recalculate the
  // properties for the dirty renderers and notify the users as appropriate.

  // If we cannot promote our own weak pointer, something is seriously wrong.
  FXL_DCHECK(owner_);
  owner_->GetDeviceManager().SelectOutputsForRenderer(this);
}

void AudioRenderer1Impl::GetPacketConsumer(
    fidl::InterfaceRequest<fuchsia::media::MediaPacketConsumer>
        consumer_request) {
  // Bind our pipe to the interface request.
  if (pipe_.is_bound()) {
    pipe_.Reset();
  }

  pipe_.Bind(std::move(consumer_request));
}

void AudioRenderer1Impl::GetTimelineControlPoint(
    fidl::InterfaceRequest<fuchsia::media::MediaTimelineControlPoint> req) {
  timeline_control_point_.Bind(std::move(req));
}

void AudioRenderer1Impl::SetGain(float db_gain) {
  if (db_gain > fuchsia::media::kMaxGain) {
    FXL_LOG(ERROR) << "Gain value too large (" << db_gain
                   << ") for audio renderer.";
    Shutdown();
    return;
  }

  db_gain_ = db_gain;

  {
    fbl::AutoLock links_lock(&links_lock_);
    for (const auto& link : dest_links_) {
      FXL_DCHECK(link && link->source_type() == AudioLink::SourceType::Packet);
      auto packet_link = static_cast<AudioLinkPacketSource*>(link.get());
      packet_link->gain().SetRendererGain(db_gain_);
    }
  }
}

void AudioRenderer1Impl::GetMinDelay(GetMinDelayCallback callback) {
  callback(min_clock_lead_nsec_);
}

zx_status_t AudioRenderer1Impl::InitializeDestLink(const AudioLinkPtr& link) {
  FXL_DCHECK(link);
  FXL_DCHECK(link->valid());
  FXL_DCHECK(link->GetSource().get() == static_cast<AudioObject*>(this));
  FXL_DCHECK(link->source_type() == AudioLink::SourceType::Packet);

  auto packet_link = static_cast<AudioLinkPacketSource*>(link.get());
  packet_link->gain().SetRendererGain(db_gain_);

  // Prime this new link with the pending contents of the throttle output.
  if (throttle_output_link_ != nullptr) {
    packet_link->CopyPendingQueue(throttle_output_link_);
  }

  return ZX_OK;
}

void AudioRenderer1Impl::OnRenderRange(int64_t presentation_time,
                                       uint32_t duration) {}

void AudioRenderer1Impl::SnapshotCurrentTimelineFunction(int64_t ref_time,
                                                         TimelineFunction* out,
                                                         uint32_t* generation) {
  FXL_DCHECK(out != nullptr);
  FXL_DCHECK(generation != nullptr);

  TimelineFunction tcp_fn;
  uint32_t tcp_gen;

  timeline_control_point_.SnapshotCurrentFunction(ref_time, &tcp_fn, &tcp_gen);

  if (*generation != tcp_gen) {
    // The control point works in ns units. We want the rate in frames per
    // nanosecond, so we convert here.
    TimelineRate frac_frames_per_ns =
        format_info()->frames_per_ns() *
        TimelineRate(1u << kPtsFractionalBits, 1u);

    TimelineRate rate_in_frames_per_ns = tcp_fn.rate() * frac_frames_per_ns;

    *out = TimelineFunction(
        tcp_fn.subject_time() * format_info()->frames_per_ns(),
        tcp_fn.reference_time(), rate_in_frames_per_ns.subject_delta(),
        rate_in_frames_per_ns.reference_delta());

    *generation = tcp_gen;
  }
}

void AudioRenderer1Impl::OnPacketReceived(fbl::RefPtr<AudioPacketRef> packet) {
  FXL_DCHECK(packet);
  FXL_DCHECK(format_info_valid());

  {
    fbl::AutoLock links_lock(&links_lock_);
    for (const auto& link : dest_links_) {
      FXL_DCHECK(link && link->source_type() == AudioLink::SourceType::Packet);
      auto packet_link = static_cast<AudioLinkPacketSource*>(link.get());
      packet_link->PushToPendingQueue(packet);
    }
  }

  if (packet->flags() & fuchsia::media::kFlagEos) {
    timeline_control_point_.SetEndOfStreamPts(
        (packet->end_pts() >> kPtsFractionalBits) /
        format_info_->frames_per_ns());
  }
}

bool AudioRenderer1Impl::OnFlushRequested(
    fuchsia::media::MediaPacketConsumer::FlushCallback cbk) {
  fbl::RefPtr<PendingFlushToken> flush_token =
      PendingFlushToken::Create(owner_, std::move(cbk));

  {
    fbl::AutoLock links_lock(&links_lock_);
    for (const auto& link : dest_links_) {
      FXL_DCHECK(link && link->source_type() == AudioLink::SourceType::Packet);
      auto packet_link = static_cast<AudioLinkPacketSource*>(link.get());
      packet_link->FlushPendingQueue(flush_token);
    }
  }

  timeline_control_point_.ClearEndOfStream();

  return true;
}

fidl::VectorPtr<fuchsia::media::MediaTypeSet>
AudioRenderer1Impl::SupportedMediaTypes() {
  // Build a minimal descriptor
  //
  // TODO(johngro): one day, we need to make this description much more rich and
  // fully describe our capabilities, based on things like what outputs are
  // available, the class of hardware we are on, and what options we were
  // compiled with.
  //
  // For now, it would be nice to just be able to have a static const tree of
  // capabilities in this translational unit which we could use to construct our
  // message, but the nature of the structures generated by the C++ bindings
  // make this difficult.  For now, we just create a trivial descriptor entierly
  // by hand.
  fidl::VectorPtr<fuchsia::media::MediaTypeSet> supported_media_types(
      arraysize(kSupportedAudioTypeSets));

  for (size_t i = 0; i < supported_media_types->size(); ++i) {
    fuchsia::media::MediaTypeSet& mts = supported_media_types->at(i);

    mts.medium = fuchsia::media::MediaTypeMedium::AUDIO;
    mts.encodings.push_back(fuchsia::media::kAudioEncodingLpcm);

    const auto& s = kSupportedAudioTypeSets[i];
    fuchsia::media::AudioMediaTypeSetDetails audio_detail;
    audio_detail.sample_format = s.sample_format;
    audio_detail.min_channels = s.min_channels;
    audio_detail.max_channels = s.max_channels;
    audio_detail.min_frames_per_second = s.min_frames_per_second;
    audio_detail.max_frames_per_second = s.max_frames_per_second;
    mts.details.set_audio(std::move(audio_detail));
  }

  return supported_media_types;
}

}  // namespace audio
}  // namespace media
