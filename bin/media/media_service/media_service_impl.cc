// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/media_service_impl.h"

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

namespace media {

MediaServiceImpl::MediaServiceImpl() {
  FLOG_INITIALIZE(application_context(), "media_service");

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

void MediaServiceImpl::CreateSink(fidl::InterfaceHandle<MediaRenderer> renderer,
                                  MediaTypePtr media_type,
                                  fidl::InterfaceRequest<MediaSink> sink) {
  AddProduct(MediaSinkImpl::Create(std::move(renderer), std::move(media_type),
                                   std::move(sink), this));
}

void MediaServiceImpl::CreateDemux(fidl::InterfaceHandle<SeekingReader> reader,
                                   fidl::InterfaceRequest<MediaSource> demux) {
  AddProduct(MediaDemuxImpl::Create(std::move(reader), std::move(demux), this));
}

void MediaServiceImpl::CreateDecoder(
    MediaTypePtr input_media_type,
    fidl::InterfaceRequest<MediaTypeConverter> decoder) {
  AddProduct(MediaDecoderImpl::Create(std::move(input_media_type),
                                      std::move(decoder), this));
}

void MediaServiceImpl::CreateNetworkReader(
    const fidl::String& url,
    fidl::InterfaceRequest<SeekingReader> reader) {
  AddProduct(NetworkReaderImpl::Create(url, std::move(reader), this));
}

void MediaServiceImpl::CreateFileReader(
    const fidl::String& path,
    fidl::InterfaceRequest<SeekingReader> reader) {
  AddProduct(FileReaderImpl::Create(path, std::move(reader), this));
}

void MediaServiceImpl::CreateAudioRenderer(
    fidl::InterfaceRequest<AudioRenderer> audio_renderer,
    fidl::InterfaceRequest<MediaRenderer> media_renderer) {
  AudioServerPtr audio_service =
      application_context()->ConnectToEnvironmentService<media::AudioServer>();

  audio_service->CreateRenderer(std::move(audio_renderer),
                                std::move(media_renderer));
}

void MediaServiceImpl::CreateVideoRenderer(
    fidl::InterfaceRequest<VideoRenderer> video_renderer,
    fidl::InterfaceRequest<MediaRenderer> media_renderer) {
  AddProduct(VideoRendererImpl::Create(std::move(video_renderer),
                                       std::move(media_renderer), this));
}

void MediaServiceImpl::CreateAudioCapturer(
    fidl::InterfaceRequest<MediaCapturer> media_capturer) {
  AddProduct(AudioCapturerImpl::Create(std::move(media_capturer), this));
}

void MediaServiceImpl::CreateTimelineController(
    fidl::InterfaceRequest<MediaTimelineController> timeline_controller) {
  AddProduct(MediaTimelineControllerImpl::Create(std::move(timeline_controller),
                                                 this));
}

void MediaServiceImpl::CreateLpcmReformatter(
    MediaTypePtr input_media_type,
    AudioSampleFormat output_sample_format,
    fidl::InterfaceRequest<MediaTypeConverter> lpcm_reformatter) {
  AddProduct(LpcmReformatterImpl::Create(std::move(input_media_type),
                                         output_sample_format,
                                         std::move(lpcm_reformatter), this));
}

}  // namespace media
