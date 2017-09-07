// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/media_service_impl.h"

#include <magenta/syscalls.h>

#include "apps/media/services/audio_policy_service.fidl.h"
#include "apps/media/services/audio_server.fidl.h"
#include "apps/media/src/media_service/audio_capturer_impl.h"
#include "apps/media/src/media_service/file_reader_impl.h"
#include "apps/media/src/media_service/lpcm_reformatter_impl.h"
#include "apps/media/src/media_service/media_decoder_impl.h"
#include "apps/media/src/media_service/media_demux_impl.h"
#include "apps/media/src/media_service/media_player_impl.h"
#include "apps/media/src/media_service/media_sink_impl.h"
#include "apps/media/src/media_service/media_source_impl.h"
#include "apps/media/src/media_service/media_timeline_controller_impl.h"
#include "apps/media/src/media_service/network_reader_impl.h"
#include "apps/media/src/media_service/video_renderer_impl.h"
#include "apps/media/src/util/multiproc_task_runner.h"
#include "lib/ftl/functional/make_copyable.h"

namespace media {

MediaServiceImpl::MediaServiceImpl(
    std::unique_ptr<app::ApplicationContext> context)
    : FactoryServiceBase(std::move(context)) {
  FLOG_INITIALIZE(application_context(), "media_service");

  multiproc_task_runner_ =
      AdoptRef(new MultiprocTaskRunner(mx_system_get_num_cpus()));

  application_context()->outgoing_services()->AddService<MediaService>(
      [this](fidl::InterfaceRequest<MediaService> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

MediaServiceImpl::~MediaServiceImpl() {
  FLOG_DESTROY();
}

void MediaServiceImpl::CreatePlayer(
    fidl::InterfaceHandle<SeekingReader> reader,
    fidl::InterfaceHandle<MediaRenderer> audio_renderer,
    fidl::InterfaceHandle<MediaRenderer> video_renderer,
    fidl::InterfaceRequest<MediaPlayer> player) {
  AddProduct(MediaPlayerImpl::Create(
      std::move(reader), std::move(audio_renderer), std::move(video_renderer),
      std::move(player), this));
}

void MediaServiceImpl::CreateSource(
    fidl::InterfaceHandle<SeekingReader> reader,
    fidl::Array<MediaTypeSetPtr> media_types,
    fidl::InterfaceRequest<MediaSource> source) {
  AddProduct(MediaSourceImpl::Create(std::move(reader), media_types,
                                     std::move(source), this));
}

void MediaServiceImpl::CreateSink(
    fidl::InterfaceHandle<MediaRenderer> renderer,
    fidl::InterfaceRequest<MediaSink> sink_request) {
  AddProduct(MediaSinkImpl::Create(std::move(renderer), std::move(sink_request),
                                   this));
}

void MediaServiceImpl::CreateDemux(
    fidl::InterfaceHandle<SeekingReader> reader,
    fidl::InterfaceRequest<MediaSource> request) {
  CreateProductOnNewThread<MediaDemuxImpl>(ftl::MakeCopyable([
    this, reader = std::move(reader), request = std::move(request)
  ]() mutable {
    return MediaDemuxImpl::Create(std::move(reader), std::move(request), this);
  }));
}

void MediaServiceImpl::CreateDecoder(
    MediaTypePtr input_media_type,
    fidl::InterfaceRequest<MediaTypeConverter> request) {
  CreateProductOnNewThread<MediaDecoderImpl>(ftl::MakeCopyable([
    this, input_media_type = std::move(input_media_type),
    request = std::move(request)
  ]() mutable {
    return MediaDecoderImpl::Create(std::move(input_media_type),
                                    std::move(request), this);
  }));
}

void MediaServiceImpl::CreateNetworkReader(
    const fidl::String& url,
    fidl::InterfaceRequest<SeekingReader> request) {
  CreateProductOnNewThread<NetworkReaderImpl>(ftl::MakeCopyable(
      [ this, url = url, request = std::move(request) ]() mutable {
        return NetworkReaderImpl::Create(url, std::move(request), this);
      }));
}

void MediaServiceImpl::CreateFileReader(
    const fidl::String& path,
    fidl::InterfaceRequest<SeekingReader> request) {
  CreateProductOnNewThread<FileReaderImpl>(ftl::MakeCopyable(
      [ this, path = path, request = std::move(request) ]() mutable {
        return FileReaderImpl::Create(path, std::move(request), this);
      }));
}

void MediaServiceImpl::CreateAudioRenderer(
    fidl::InterfaceRequest<AudioRenderer> audio_renderer_request,
    fidl::InterfaceRequest<MediaRenderer> media_renderer_request) {
  AudioServerPtr audio_service =
      application_context()->ConnectToEnvironmentService<media::AudioServer>();

  // Ensure that the audio policy service is running so that the system audio
  // gain is properly set.
  // TODO(dalesat): Remove this when the policy service owns creating renderers.
  application_context()
      ->ConnectToEnvironmentService<media::AudioPolicyService>();

  audio_service->CreateRenderer(std::move(audio_renderer_request),
                                std::move(media_renderer_request));
}

void MediaServiceImpl::CreateVideoRenderer(
    fidl::InterfaceRequest<VideoRenderer> video_renderer_request,
    fidl::InterfaceRequest<MediaRenderer> media_renderer_request) {
  CreateProductOnNewThread<VideoRendererImpl>(ftl::MakeCopyable([
    this, video_renderer_request = std::move(video_renderer_request),
    media_renderer_request = std::move(media_renderer_request)
  ]() mutable {
    return VideoRendererImpl::Create(std::move(video_renderer_request),
                                     std::move(media_renderer_request), this);
  }));
}

void MediaServiceImpl::CreateAudioCapturer(
    fidl::InterfaceRequest<MediaCapturer> request) {
  CreateProductOnNewThread<AudioCapturerImpl>(
      ftl::MakeCopyable([ this, request = std::move(request) ]() mutable {
        return AudioCapturerImpl::Create(std::move(request), this);
      }));
}

void MediaServiceImpl::CreateTimelineController(
    fidl::InterfaceRequest<MediaTimelineController> timeline_controller) {
  AddProduct(MediaTimelineControllerImpl::Create(std::move(timeline_controller),
                                                 this));
}

void MediaServiceImpl::CreateLpcmReformatter(
    MediaTypePtr input_media_type,
    AudioSampleFormat output_sample_format,
    fidl::InterfaceRequest<MediaTypeConverter> request) {
  CreateProductOnNewThread<LpcmReformatterImpl>(ftl::MakeCopyable([
    this, input_media_type = std::move(input_media_type), output_sample_format,
    request = std::move(request)
  ]() mutable {
    return LpcmReformatterImpl::Create(std::move(input_media_type),
                                       output_sample_format, std::move(request),
                                       this);
  }));
}

}  // namespace media
