// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/audio_consumer_impl.h"

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fidl/cpp/type_converter.h>
#include <lib/fit/function.h>
#include <lib/media/cpp/type_converters.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <lib/zx/clock.h>
#include <zircon/types.h>

#include <memory>
#include <sstream>

#include <fs/pseudo_file.h>

#include "lib/fidl/cpp/interface_request.h"
#include "lib/fit/result.h"
#include "lib/media/cpp/timeline_rate.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/media/playback/mediaplayer/core/renderer_sink_segment.h"
#include "src/media/playback/mediaplayer/fidl/fidl_audio_renderer.h"
#include "src/media/playback/mediaplayer/fidl/fidl_reader.h"
#include "src/media/playback/mediaplayer/fidl/fidl_type_conversions.h"
#include "src/media/playback/mediaplayer/fidl/fidl_video_renderer.h"
#include "src/media/playback/mediaplayer/fidl/simple_stream_sink_impl.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"
#include "src/media/playback/mediaplayer/graph/thread_priority.h"
#include "src/media/playback/mediaplayer/graph/types/stream_type.h"
#include "src/media/playback/mediaplayer/source_impl.h"
#include "src/media/playback/mediaplayer/util/safe_clone.h"
#include "zircon/third_party/uapp/dash/src/output.h"

namespace media_player {

// static
std::unique_ptr<SessionAudioConsumerFactoryImpl> SessionAudioConsumerFactoryImpl::Create(
    fidl::InterfaceRequest<fuchsia::media::SessionAudioConsumerFactory> request,
    sys::ComponentContext* component_context, fit::closure quit_callback) {
  return std::make_unique<SessionAudioConsumerFactoryImpl>(std::move(request), component_context,
                                                           std::move(quit_callback));
}

SessionAudioConsumerFactoryImpl::SessionAudioConsumerFactoryImpl(
    fidl::InterfaceRequest<fuchsia::media::SessionAudioConsumerFactory> request,
    sys::ComponentContext* component_context, fit::closure quit_callback)
    : component_context_(component_context), quit_callback_(std::move(quit_callback)) {
  FX_DCHECK(component_context_);
  FX_DCHECK(quit_callback_);

  binding_ = std::make_unique<BindingType>(this, std::move(request));

  binding_->set_error_handler([this](zx_status_t status) {
    // don't quit when factory channel closes unless no audio consumers are active
    if (audio_consumer_bindings_.size() == 0 && quit_callback_) {
      quit_callback_();
      quit_callback_ = nullptr;
    }
  });

  audio_consumer_bindings_.set_empty_set_handler([this]() {
    if (quit_callback_) {
      quit_callback_();
      quit_callback_ = nullptr;
    }
  });
}

void SessionAudioConsumerFactoryImpl::CreateAudioConsumer(
    uint64_t session_id,
    fidl::InterfaceRequest<fuchsia::media::AudioConsumer> audio_consumer_request) {
  FX_DCHECK(audio_consumer_request);

  // binding set wants the underlying impl, so construct one manually and let bindings_ take over
  // ownership
  std::unique_ptr<fuchsia::media::AudioConsumer> audio_consumer(
      new AudioConsumerImpl(session_id, component_context_));
  audio_consumer_bindings_.AddBinding(std::move(audio_consumer), std::move(audio_consumer_request));
}

// static
std::unique_ptr<AudioConsumerImpl> AudioConsumerImpl::Create(
    uint64_t session_id, sys::ComponentContext* component_context) {
  return std::make_unique<AudioConsumerImpl>(session_id, component_context);
}

AudioConsumerImpl::AudioConsumerImpl(uint64_t session_id, sys::ComponentContext* component_context)
    : dispatcher_(async_get_default_dispatcher()),
      component_context_(component_context),
      core_(dispatcher_),
      status_dirty_(true) {
  FX_DCHECK(component_context_);

  ThreadPriority::SetToHigh();

  decoder_factory_ = DecoderFactory::Create(this);
  FX_DCHECK(decoder_factory_);

  core_.SetUpdateCallback([this]() {
    if (core_.problem()) {
      if (core_.problem()->type == fuchsia::media::playback::PROBLEM_AUDIO_ENCODING_NOT_SUPPORTED) {
        FX_LOGS(WARNING) << "Unsupported codec";
        if (simple_stream_sink_) {
          core_.ClearSourceSegment();
          simple_stream_sink_->Close(ZX_ERR_INVALID_ARGS);
          simple_stream_sink_ = nullptr;
        }
      }
    }
    SendStatusUpdate();
  });
}

AudioConsumerImpl::~AudioConsumerImpl() { core_.SetUpdateCallback(nullptr); }

void AudioConsumerImpl::CreateStreamSink(
    std::vector<zx::vmo> buffers, fuchsia::media::AudioStreamType audio_stream_type,
    std::unique_ptr<fuchsia::media::Compression> compression,
    fidl::InterfaceRequest<fuchsia::media::StreamSink> stream_sink_request) {
  FX_DCHECK(stream_sink_request);

  std::string encoding = fuchsia::media::AUDIO_ENCODING_LPCM;
  std::unique_ptr<Bytes> encoding_parameters;

  if (compression && !compression->type.empty()) {
    encoding = std::move(compression->type);
    if (compression->parameters) {
      encoding_parameters = fidl::To<std::unique_ptr<media_player::Bytes>>(compression->parameters);
    }
  }

  media_player::AudioStreamType stream_type(
      nullptr, encoding, std::move(encoding_parameters),
      fidl::To<media_player::AudioStreamType::SampleFormat>(audio_stream_type.sample_format),
      audio_stream_type.channels, audio_stream_type.frames_per_second);

  // Only allow one pending stream sink.
  // Setup timeline rate in ns units per interface docs.
  pending_simple_stream_sink_ = SimpleStreamSinkImpl::Create(
      stream_type, media::TimelineRate::NsPerSecond, std::move(stream_sink_request),
      /* connection_failure_callback= */
      [this]() {
        // On disconnect, check for any pending sinks
        MaybeSetNewSource();
      });

  pending_buffers_ = std::move(buffers);

  simple_stream_sink_ = pending_simple_stream_sink_;

  if (!core_.has_source_segment()) {
    MaybeSetNewSource();
  }
}

void AudioConsumerImpl::MaybeSetNewSource() {
  if (core_.has_source_segment()) {
    core_.ClearSourceSegment();
  }

  if (!pending_simple_stream_sink_) {
    return;
  }

  auto simple_stream_sink = std::move(pending_simple_stream_sink_);
  auto buffers = std::move(pending_buffers_);

  auto audio_consumer_source = AudioConsumerSourceImpl::Create(core_.graph(), []() {});
  audio_consumer_source->AddStream(simple_stream_sink, simple_stream_sink->output_stream_type());

  EnsureRenderer();

  core_.SetSourceSegment(audio_consumer_source->TakeSourceSegment(),
                         [this, simple_stream_sink = std::move(simple_stream_sink),
                          buffers = std::move(buffers)]() mutable {
                           size_t buffer_index = buffers.size() - 1;
                           while (!buffers.empty()) {
                             simple_stream_sink->AddPayloadBuffer(buffer_index--,
                                                                  std::move(*(buffers.end() - 1)));
                             buffers.pop_back();
                           }

                           SendStatusUpdate();
                         });
}

void AudioConsumerImpl::EnsureRenderer() {
  if (core_.has_sink_segment(StreamType::Medium::kAudio)) {
    // Renderer already exists.
    return;
  }

  if (!audio_renderer_) {
    auto audio = ServiceProvider::ConnectToService<fuchsia::media::Audio>();
    fuchsia::media::AudioRendererPtr audio_renderer;
    audio->CreateAudioRenderer(audio_renderer.NewRequest());
    audio_renderer_ = FidlAudioRenderer::Create(std::move(audio_renderer));
    core_.SetSinkSegment(RendererSinkSegment::Create(audio_renderer_, decoder_factory_.get()),
                         StreamType::Medium::kAudio);
  }
}

void AudioConsumerImpl::SetTimelineFunction(float rate, int64_t subject_time,
                                            int64_t reference_time, fit::closure callback) {
  core_.SetTimelineFunction(
      media::TimelineFunction(subject_time, reference_time, media::TimelineRate(rate)),
      std::move(callback));
  // TODO(afoxley) update status
}

void AudioConsumerImpl::Start(fuchsia::media::AudioConsumerStartFlags flags, int64_t reference_time,
                              int64_t media_time) {
  // TODO(afoxley) set lead time based on flags?
  timeline_started_ = true;
  SetTimelineFunction(1.0f, 0, zx::clock::get_monotonic().get() + kMinimumLeadTime, [this]() {
    core_.SetProgramRange(0, 0, Packet::kMaxPts);
    core_.Prime([]() {});
  });
}

void AudioConsumerImpl::SetRate(float rate) {
  // TODO(afoxley) pass rate through to audio renderer
}

void AudioConsumerImpl::BindVolumeControl(
    fidl::InterfaceRequest<fuchsia::media::audio::VolumeControl> request) {
  // TODO(afoxley) setup volume control
}

void AudioConsumerImpl::Stop() {
  SetTimelineFunction(0.0f, 0, zx::clock::get_monotonic().get(), []() {});
}

void AudioConsumerImpl::WatchStatus(fuchsia::media::AudioConsumer::WatchStatusCallback callback) {
  watch_status_callback_ = std::move(callback);

  if (status_dirty_) {
    status_dirty_ = false;
    SendStatusUpdate();
  }
}

void AudioConsumerImpl::ConnectToService(std::string service_path, zx::channel channel) {
  FX_DCHECK(component_context_);
  component_context_->svc()->Connect(service_path, std::move(channel));
}

void AudioConsumerImpl::SendStatusUpdate() {
  if (!watch_status_callback_) {
    status_dirty_ = true;
    return;
  }

  fuchsia::media::AudioConsumerStatus status;

  status.set_max_lead_time(kMaximumLeadTime);
  status.set_min_lead_time(kMinimumLeadTime);

  if (timeline_started_) {
    status.set_presentation_timeline(
        fidl::To<fuchsia::media::TimelineFunction>(core_.timeline_function()));
  }

  // TODO(afoxley) set any error here

  watch_status_callback_(std::move(status));
  watch_status_callback_ = nullptr;
}

}  // namespace media_player
