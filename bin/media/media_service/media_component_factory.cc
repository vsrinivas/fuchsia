// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_service/media_component_factory.h"

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
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"

namespace media {

MediaComponentFactory::MediaComponentFactory(
    std::unique_ptr<component::ApplicationContext> context)
    : FactoryServiceBase(std::move(context)),
      task_runner_(fsl::MessageLoop::GetCurrent()->task_runner()) {}

MediaComponentFactory::~MediaComponentFactory() {}

void MediaComponentFactory::CreateMediaPlayer(
    fidl::InterfaceRequest<MediaPlayer> player) {
  AddProduct(MediaPlayerImpl::Create(std::move(player), this));
}

void MediaComponentFactory::CreateSource(
    fidl::InterfaceHandle<SeekingReader> reader,
    fidl::VectorPtr<MediaTypeSet> media_types,
    fidl::InterfaceRequest<MediaSource> source) {
  AddProduct(MediaSourceImpl::Create(std::move(reader), std::move(media_types),
                                     std::move(source), this));
}

void MediaComponentFactory::CreateSink(
    fidl::InterfaceHandle<MediaRenderer> renderer,
    fidl::InterfaceRequest<MediaSink> sink_request) {
  AddProduct(MediaSinkImpl::Create(std::move(renderer), std::move(sink_request),
                                   this));
}

void MediaComponentFactory::CreateDemux(
    fidl::InterfaceHandle<SeekingReader> reader,
    fidl::InterfaceRequest<MediaSource> request) {
  AddProduct(
      MediaDemuxImpl::Create(std::move(reader), std::move(request), this));
}

void MediaComponentFactory::CreateDecoder(
    MediaTypePtr input_media_type,
    fidl::InterfaceRequest<MediaTypeConverter> request) {
  AddProduct(MediaDecoderImpl::Create(std::move(input_media_type),
                                      std::move(request), this));
}

void MediaComponentFactory::CreateTimelineController(
    fidl::InterfaceRequest<MediaTimelineController> timeline_controller) {
  AddProduct(MediaTimelineControllerImpl::Create(std::move(timeline_controller),
                                                 this));
}

void MediaComponentFactory::CreateLpcmReformatter(
    MediaTypePtr input_media_type,
    AudioSampleFormat output_sample_format,
    fidl::InterfaceRequest<MediaTypeConverter> request) {
  AddProduct(LpcmReformatterImpl::Create(std::move(input_media_type),
                                         output_sample_format,
                                         std::move(request), this));
}

void MediaComponentFactory::CreateHttpReader(
    const std::string& http_url,
    fidl::InterfaceRequest<SeekingReader> request) {
  AddProduct(NetworkReaderImpl::Create(http_url, std::move(request), this));
}

void MediaComponentFactory::CreateFileChannelReader(
    zx::channel file_channel,
    fidl::InterfaceRequest<SeekingReader> request) {
  AddProduct(FileReaderImpl::Create(std::move(file_channel), std::move(request),
                                    this));
}

std::shared_ptr<VideoRendererImpl> MediaComponentFactory::CreateVideoRenderer(
    fidl::InterfaceRequest<MediaRenderer> media_renderer_request) {
  std::shared_ptr<VideoRendererImpl> result =
      VideoRendererImpl::Create(std::move(media_renderer_request), this);
  AddProduct(result);
  return result;
}

void MediaComponentFactory::OnLastProductRemoved() {
  task_runner_->PostTask(
      []() { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
}

}  // namespace media
