// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio_server/audio_renderer_impl.h"

#include <algorithm>
#include <limits>

#include "apps/media/lib/timeline/timeline.h"
#include "apps/media/lib/timeline/timeline_function.h"
#include "apps/media/lib/timeline/timeline_rate.h"
#include "apps/media/src/audio_server/audio_output_manager.h"
#include "apps/media/src/audio_server/audio_renderer_to_output_link.h"
#include "apps/media/src/audio_server/audio_server_impl.h"
#include "lib/ftl/arraysize.h"
#include "lib/ftl/logging.h"

namespace media {
namespace audio {

constexpr size_t AudioRendererImpl::PTS_FRACTIONAL_BITS;

// TODO(johngro): If there is ever a better way to do this type of static-table
// initialization using fidl generated structs, we should switch to it.
static const struct {
  AudioSampleFormat sample_format;
  uint32_t min_channels;
  uint32_t max_channels;
  uint32_t min_frames_per_second;
  uint32_t max_frames_per_second;
} kSupportedAudioTypeSets[] = {
    {
        .sample_format = AudioSampleFormat::UNSIGNED_8,
        .min_channels = 1,
        .max_channels = 2,
        .min_frames_per_second = 1000,
        .max_frames_per_second = 48000,
    },
    {
        .sample_format = AudioSampleFormat::SIGNED_16,
        .min_channels = 1,
        .max_channels = 2,
        .min_frames_per_second = 1000,
        .max_frames_per_second = 48000,
    },
};

AudioRendererImpl::AudioRendererImpl(
    fidl::InterfaceRequest<AudioRenderer> audio_renderer_request,
    fidl::InterfaceRequest<MediaRenderer> media_renderer_request,
    AudioServerImpl* owner)
    : owner_(owner),
      audio_renderer_binding_(this, std::move(audio_renderer_request)),
      media_renderer_binding_(this, std::move(media_renderer_request)),
      pipe_(this, owner) {
  FTL_CHECK(nullptr != owner_);

  audio_renderer_binding_.set_connection_error_handler([this]() -> void {
    audio_renderer_binding_.set_connection_error_handler(nullptr);
    audio_renderer_binding_.Close();

    // If the media_renderer binding has also been closed, it is time to shut
    // down.
    if (!media_renderer_binding_.is_bound()) {
      Shutdown();
    }
  });

  media_renderer_binding_.set_connection_error_handler([this]() -> void {
    media_renderer_binding_.set_connection_error_handler(nullptr);
    media_renderer_binding_.Close();

    // If the audio_renderer binding has also been closed, it is time to shut
    // down.
    if (!audio_renderer_binding_.is_bound()) {
      Shutdown();
    }
  });

  timeline_control_point_.SetPrimeRequestedCallback(
      [this](const TimelineControlPoint::PrimeCallback& callback) {
        pipe_.PrimeRequested(callback);
      });
}

AudioRendererImpl::~AudioRendererImpl() {
  // assert that we have been cleanly shutdown already.
  FTL_DCHECK(!audio_renderer_binding_.is_bound());
  FTL_DCHECK(!media_renderer_binding_.is_bound());
}

AudioRendererImplPtr AudioRendererImpl::Create(
    fidl::InterfaceRequest<AudioRenderer> audio_renderer_request,
    fidl::InterfaceRequest<MediaRenderer> media_renderer_request,
    AudioServerImpl* owner) {
  AudioRendererImplPtr ret(
      new AudioRendererImpl(std::move(audio_renderer_request),
                            std::move(media_renderer_request), owner));
  ret->weak_this_ = ret;
  return ret;
}

void AudioRendererImpl::Shutdown() {

  // If we have already been shutdown, then we are just waiting for the service
  // to destroy us.  Run some FTL_DCHECK sanity checks and get out.
  if (is_shutdown_) {
    FTL_DCHECK(!audio_renderer_binding_.is_bound());
    FTL_DCHECK(!media_renderer_binding_.is_bound());
    FTL_DCHECK(!pipe_.is_bound());
    FTL_DCHECK(!timeline_control_point_.is_bound());
    FTL_DCHECK(!output_links_.size());
    return;
  }

  is_shutdown_ = true;

  if (audio_renderer_binding_.is_bound()) {
    audio_renderer_binding_.set_connection_error_handler(nullptr);
    audio_renderer_binding_.Close();
  }

  if (media_renderer_binding_.is_bound()) {
    media_renderer_binding_.set_connection_error_handler(nullptr);
    media_renderer_binding_.Close();
  }

  // reset all of our internal state and close any other client connections in
  // the process.
  pipe_.Reset();
  timeline_control_point_.Reset();
  output_links_.clear();
  throttle_output_link_ = nullptr;

  FTL_DCHECK(owner_);
  AudioRendererImplPtr thiz = weak_this_.lock();
  owner_->GetOutputManager().RemoveRenderer(thiz);
}

void AudioRendererImpl::GetSupportedMediaTypes(
    const GetSupportedMediaTypesCallback& cbk) {
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
  fidl::Array<MediaTypeSetPtr> supported_media_types =
      fidl::Array<MediaTypeSetPtr>::New(arraysize(kSupportedAudioTypeSets));

  for (size_t i = 0; i < supported_media_types.size(); ++i) {
    const MediaTypeSetPtr& mts =
        (supported_media_types[i] = MediaTypeSet::New());

    mts->medium = MediaTypeMedium::AUDIO;
    mts->encodings = fidl::Array<fidl::String>::New(1);
    mts->details = MediaTypeSetDetails::New();

    mts->encodings[0] = MediaType::kAudioEncodingLpcm;

    const auto& s = kSupportedAudioTypeSets[i];
    AudioMediaTypeSetDetailsPtr audio_detail = AudioMediaTypeSetDetails::New();

    audio_detail->sample_format = s.sample_format;
    audio_detail->min_channels = s.min_channels;
    audio_detail->max_channels = s.max_channels;
    audio_detail->min_frames_per_second = s.min_frames_per_second;
    audio_detail->max_frames_per_second = s.max_frames_per_second;
    mts->details->set_audio(std::move(audio_detail));
  }

  cbk(std::move(supported_media_types));
}

void AudioRendererImpl::SetMediaType(MediaTypePtr media_type) {
  // Check the requested configuration.
  if ((media_type->medium != MediaTypeMedium::AUDIO) ||
      (media_type->encoding != MediaType::kAudioEncodingLpcm) ||
      (!media_type->details->is_audio())) {
    FTL_LOG(ERROR)
        << "Unsupported configuration requested in "
           "AudioRenderer::Configure.  Media type must be LPCM audio.";
    Shutdown();
    return;
  }

  // Search our supported configuration sets to find one compatible with this
  // request.
  auto& cfg = media_type->details->get_audio();
  size_t i;
  for (i = 0; i < arraysize(kSupportedAudioTypeSets); ++i) {
    const auto& cfg_set = kSupportedAudioTypeSets[i];

    if ((cfg->sample_format == cfg_set.sample_format) &&
        (cfg->channels >= cfg_set.min_channels) &&
        (cfg->channels <= cfg_set.max_channels) &&
        (cfg->frames_per_second >= cfg_set.min_frames_per_second) &&
        (cfg->frames_per_second <= cfg_set.max_frames_per_second)) {
      break;
    }
  }

  if (i >= arraysize(kSupportedAudioTypeSets)) {
    FTL_LOG(ERROR) << "Unsupported LPCM configuration requested in "
                   << "AudioRenderer::Configure.  "
                   << "(format = " << cfg->sample_format
                   << ", channels = " << static_cast<uint32_t>(cfg->channels)
                   << ", frames_per_second = " << cfg->frames_per_second << ")";
    Shutdown();
    return;
  }

  frames_per_ns_ =
      TimelineRate(cfg->frames_per_second, Timeline::ns_from_seconds(1));

  pipe_.SetPtsRate(TimelineRate(cfg->frames_per_second, 1));

  // Figure out the rate we need to scale by in order to produce our fixed
  // point timestamps.
  frame_to_media_ratio_ = TimelineRate(1 << PTS_FRACTIONAL_BITS, 1);

  // Figure out how many bytes we need to hold the requested number of nSec of
  // audio.
  switch (cfg->sample_format) {
    case AudioSampleFormat::UNSIGNED_8:
      bytes_per_frame_ = 1;
      break;

    case AudioSampleFormat::SIGNED_16:
      bytes_per_frame_ = 2;
      break;

    case AudioSampleFormat::SIGNED_24_IN_32:
      bytes_per_frame_ = 4;
      break;

    default:
      FTL_DCHECK(false) << "unrecognized sample format";
      bytes_per_frame_ = 2;
      break;
  }
  bytes_per_frame_ *= cfg->channels;

  // Stash our configuration.
  format_ = std::move(cfg);

  // Have the audio output manager initialize our set of outputs.  Note; there
  // is currently no need for a lock here.  Methods called from our user-facing
  // interfaces are seriailzed by nature of the fidl framework, and none of the
  // output manager's threads should ever need to manipulate the set.  Cleanup
  // of outputs which have gone away is currently handled in a lazy fashion when
  // the renderer fails to promote its weak reference during an operation
  // involving its outputs.
  //
  // TODO(johngro): someday, we will need to deal with recalculating properties
  // which depend on a renderer's current set of outputs (for example, the
  // minimum
  // latency).  This will probably be done using a dirty flag in the renderer
  // implementations, and scheduling a job to recalculate the properties for the
  // dirty renderers and notify the users as appropriate.

  // If we cannot promote our own weak pointer, something is seriously wrong.
  AudioRendererImplPtr strong_this(weak_this_.lock());
  FTL_DCHECK(strong_this);
  FTL_DCHECK(owner_);
  owner_->GetOutputManager().SelectOutputsForRenderer(strong_this);
}

void AudioRendererImpl::GetPacketConsumer(
    fidl::InterfaceRequest<MediaPacketConsumer> consumer_request) {
  // Bind our pipe to the interface request.
  pipe_.Bind(std::move(consumer_request));
}

void AudioRendererImpl::GetTimelineControlPoint(
    fidl::InterfaceRequest<MediaTimelineControlPoint> req) {
  timeline_control_point_.Bind(std::move(req));
}

void AudioRendererImpl::SetGain(float db_gain) {
  if (db_gain >= AudioRenderer::kMaxGain) {
    FTL_LOG(ERROR) << "Gain value too large (" << db_gain
                   << ") for audio renderer.";
    Shutdown();
    return;
  }

  db_gain_ = db_gain;

  for (const auto& output : output_links_) {
    FTL_DCHECK(output);
    output->UpdateGain();
  }
}

void AudioRendererImpl::AddOutput(AudioRendererToOutputLinkPtr link) {
  // TODO(johngro): assert that we are on the main message loop thread.
  FTL_DCHECK(link);
  auto res = output_links_.emplace(link);
  FTL_DCHECK(res.second);
  link->UpdateGain();

  // Prime this new output with the pending contents of the throttle output.
  if (throttle_output_link_ != nullptr) {
    link->InitPendingQueue(throttle_output_link_);
  }
}

void AudioRendererImpl::RemoveOutput(AudioRendererToOutputLinkPtr link) {
  // TODO(johngro): assert that we are on the main message loop thread.
  FTL_DCHECK(link);

  if (link == throttle_output_link_) {
    throttle_output_link_ = nullptr;
  } else {
    auto iter = output_links_.find(link);
    if (iter != output_links_.end()) {
      output_links_.erase(iter);
    } else {
      // TODO(johngro): that's odd.  I can't think of a reason why we we should
      // not be able to find this link in our set of outputs... should we log
      // something about this?
      FTL_DCHECK(false);
    }
  }
}

void AudioRendererImpl::SetThrottleOutput(
    const AudioRendererToOutputLinkPtr& throttle_output_link) {
  // TODO(johngro): assert that we are on the main message loop thread.
  FTL_DCHECK(throttle_output_link  != nullptr);
  FTL_DCHECK(throttle_output_link_ == nullptr);
  throttle_output_link_ = throttle_output_link;
}

void AudioRendererImpl::SnapshotRateTrans(TimelineFunction* out,
                                          uint32_t* generation) {
  TimelineFunction timeline_function;
  timeline_control_point_.SnapshotCurrentFunction(
      Timeline::local_now(), &timeline_function, generation);

  // The control point works in ns units. We want the rate in frames per
  // nanosecond, so we convert here.
  TimelineRate rate_in_frames_per_ns =
      timeline_function.rate() * frames_per_ns_;

  *out = TimelineFunction(timeline_function.reference_time(),
                          timeline_function.subject_time() * frames_per_ns_,
                          rate_in_frames_per_ns.reference_delta(),
                          rate_in_frames_per_ns.subject_delta());
}

void AudioRendererImpl::OnPacketReceived(AudioPipe::AudioPacketRefPtr packet) {
  FTL_DCHECK(packet);

  if (throttle_output_link_ != nullptr) {
    throttle_output_link_->PushToPendingQueue(packet);
  }

  for (const auto& output : output_links_) {
    FTL_DCHECK(output);
    output->PushToPendingQueue(packet);
  }

  if (packet->supplied_packet()->packet()->end_of_stream) {
    FTL_DCHECK(packet->supplied_packet()->packet()->pts_rate_ticks ==
               format_->frames_per_second);
    FTL_DCHECK(packet->supplied_packet()->packet()->pts_rate_seconds == 1);
    timeline_control_point_.SetEndOfStreamPts(
        (packet->supplied_packet()->packet()->pts + packet->frame_count()) /
        frames_per_ns_);
  }
}

bool AudioRendererImpl::OnFlushRequested(
    const MediaPacketConsumer::FlushCallback& cbk) {
  if (throttle_output_link_ != nullptr) {
    throttle_output_link_->FlushPendingQueue();
  }

  for (const auto& output : output_links_) {
    FTL_DCHECK(output);
    output->FlushPendingQueue();
  }
  cbk();
  return true;
}

}  // namespace audio
}  // namespace media
