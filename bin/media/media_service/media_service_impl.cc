// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_service/media_service_impl.h"

#include <zircon/syscalls.h>

#include "garnet/bin/media/media_service/file_reader_impl.h"
#include "garnet/bin/media/media_service/lpcm_reformatter_impl.h"
#include "garnet/bin/media/media_service/media_decoder_impl.h"
#include "garnet/bin/media/media_service/media_demux_impl.h"
#include "garnet/bin/media/media_service/media_player_impl.h"
#include "garnet/bin/media/media_service/media_sink_impl.h"
#include "garnet/bin/media/media_service/media_source_impl.h"
#include "garnet/bin/media/media_service/media_timeline_controller_impl.h"
#include "garnet/bin/media/media_service/network_reader_impl.h"
#include "garnet/bin/media/media_service/video_renderer_impl.h"
#include "garnet/bin/media/util/multiproc_task_runner.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/media/fidl/audio_policy_service.fidl.h"
#include "lib/media/fidl/audio_server.fidl.h"

namespace media {

MediaServiceImpl::MediaServiceImpl(
    std::unique_ptr<app::ApplicationContext> context,
    bool transient)
    : FactoryServiceBase(std::move(context)), transient_(transient) {
  FLOG_INITIALIZE(application_context(), "media_service");

  multiproc_task_runner_ =
      AdoptRef(new MultiprocTaskRunner(zx_system_get_num_cpus()));

  application_context()->outgoing_services()->AddService<MediaService>(
      [this](f1dl::InterfaceRequest<MediaService> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

MediaServiceImpl::~MediaServiceImpl() {
  FLOG_DESTROY();
}

void MediaServiceImpl::CreateHttpPlayer(
    const f1dl::String& http_url,
    f1dl::InterfaceHandle<MediaRenderer> audio_renderer,
    f1dl::InterfaceHandle<MediaRenderer> video_renderer,
    f1dl::InterfaceRequest<MediaPlayer> player) {
  f1dl::InterfaceHandle<SeekingReader> reader;
  CreateHttpReader(http_url, reader.NewRequest());
  AddProduct(MediaPlayerImpl::Create(
      std::move(reader), std::move(audio_renderer), std::move(video_renderer),
      std::move(player), this));
}

void MediaServiceImpl::CreateFilePlayer(
    zx::channel file_channel,
    f1dl::InterfaceHandle<MediaRenderer> audio_renderer,
    f1dl::InterfaceHandle<MediaRenderer> video_renderer,
    f1dl::InterfaceRequest<MediaPlayer> player) {
  f1dl::InterfaceHandle<SeekingReader> reader;
  CreateFileChannelReader(std::move(file_channel), reader.NewRequest());
  AddProduct(MediaPlayerImpl::Create(
      std::move(reader), std::move(audio_renderer), std::move(video_renderer),
      std::move(player), this));
}

void MediaServiceImpl::CreatePlayer(
    f1dl::InterfaceHandle<SeekingReader> reader,
    f1dl::InterfaceHandle<MediaRenderer> audio_renderer,
    f1dl::InterfaceHandle<MediaRenderer> video_renderer,
    f1dl::InterfaceRequest<MediaPlayer> player) {
  AddProduct(MediaPlayerImpl::Create(
      std::move(reader), std::move(audio_renderer), std::move(video_renderer),
      std::move(player), this));
}

void MediaServiceImpl::CreateSource(
    f1dl::InterfaceHandle<SeekingReader> reader,
    f1dl::Array<MediaTypeSetPtr> media_types,
    f1dl::InterfaceRequest<MediaSource> source) {
  AddProduct(MediaSourceImpl::Create(std::move(reader), media_types,
                                     std::move(source), this));
}

void MediaServiceImpl::CreateSink(
    f1dl::InterfaceHandle<MediaRenderer> renderer,
    f1dl::InterfaceRequest<MediaSink> sink_request) {
  AddProduct(MediaSinkImpl::Create(std::move(renderer), std::move(sink_request),
                                   this));
}

void MediaServiceImpl::CreateDemux(
    f1dl::InterfaceHandle<SeekingReader> reader,
    f1dl::InterfaceRequest<MediaSource> request) {
  CreateProductOnNewThread<MediaDemuxImpl>(fxl::MakeCopyable([
    this, reader = std::move(reader), request = std::move(request)
  ]() mutable {
    return MediaDemuxImpl::Create(std::move(reader), std::move(request), this);
  }));
}

void MediaServiceImpl::CreateDecoder(
    MediaTypePtr input_media_type,
    f1dl::InterfaceRequest<MediaTypeConverter> request) {
  CreateProductOnNewThread<MediaDecoderImpl>(fxl::MakeCopyable([
    this, input_media_type = std::move(input_media_type),
    request = std::move(request)
  ]() mutable {
    return MediaDecoderImpl::Create(std::move(input_media_type),
                                    std::move(request), this);
  }));
}

void MediaServiceImpl::CreateAudioRenderer(
    f1dl::InterfaceRequest<AudioRenderer> audio_renderer_request,
    f1dl::InterfaceRequest<MediaRenderer> media_renderer_request) {
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
    f1dl::InterfaceRequest<VideoRenderer> video_renderer_request,
    f1dl::InterfaceRequest<MediaRenderer> media_renderer_request) {
  CreateProductOnNewThread<VideoRendererImpl>(fxl::MakeCopyable([
    this, video_renderer_request = std::move(video_renderer_request),
    media_renderer_request = std::move(media_renderer_request)
  ]() mutable {
    return VideoRendererImpl::Create(std::move(video_renderer_request),
                                     std::move(media_renderer_request), this);
  }));
}

void MediaServiceImpl::CreateTimelineController(
    f1dl::InterfaceRequest<MediaTimelineController> timeline_controller) {
  AddProduct(MediaTimelineControllerImpl::Create(std::move(timeline_controller),
                                                 this));
}

void MediaServiceImpl::CreateLpcmReformatter(
    MediaTypePtr input_media_type,
    AudioSampleFormat output_sample_format,
    f1dl::InterfaceRequest<MediaTypeConverter> request) {
  CreateProductOnNewThread<LpcmReformatterImpl>(fxl::MakeCopyable([
    this, input_media_type = std::move(input_media_type), output_sample_format,
    request = std::move(request)
  ]() mutable {
    return LpcmReformatterImpl::Create(std::move(input_media_type),
                                       output_sample_format, std::move(request),
                                       this);
  }));
}

void MediaServiceImpl::CreateHttpReader(
    const std::string& http_url,
    f1dl::InterfaceRequest<SeekingReader> request) {
  CreateProductOnNewThread<NetworkReaderImpl>(fxl::MakeCopyable(
      [ this, http_url = http_url, request = std::move(request) ]() mutable {
        return NetworkReaderImpl::Create(http_url, std::move(request), this);
      }));
}

void MediaServiceImpl::CreateFileChannelReader(
    zx::channel file_channel,
    f1dl::InterfaceRequest<SeekingReader> request) {
  CreateProductOnNewThread<FileReaderImpl>(fxl::MakeCopyable([
    this, file_channel = std::move(file_channel), request = std::move(request)
  ]() mutable {
    return FileReaderImpl::Create(std::move(file_channel), std::move(request),
                                  this);
  }));
}

void MediaServiceImpl::OnLastProductRemoved() {
  if (transient_) {
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  }
}

}  // namespace media
