// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/media/factory_service/factory_service.h"

#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/services/flog/cpp/flog.h"
#include "services/media/factory_service/media_decoder_impl.h"
#include "services/media/factory_service/media_demux_impl.h"
#include "services/media/factory_service/media_player_impl.h"
#include "services/media/factory_service/media_sink_impl.h"
#include "services/media/factory_service/media_source_impl.h"
#include "services/media/factory_service/media_timeline_controller_impl.h"
#include "services/media/factory_service/network_reader_impl.h"

namespace mojo {
namespace media {

MediaFactoryService::MediaFactoryService() {}

MediaFactoryService::~MediaFactoryService() {
  FLOG_DESTROY();
}

void MediaFactoryService::OnInitialize() {
  FLOG_INITIALIZE(shell(), "media_factory");
}

bool MediaFactoryService::OnAcceptConnection(
    ServiceProviderImpl* service_provider_impl) {
  service_provider_impl->AddService<MediaFactory>(
      [this](const ConnectionContext& connection_context,
             InterfaceRequest<MediaFactory> media_factory_request) {
        bindings_.AddBinding(this, media_factory_request.Pass());
      });
  return true;
}

void MediaFactoryService::CreatePlayer(
    InterfaceHandle<SeekingReader> reader,
    InterfaceHandle<MediaRenderer> audio_renderer,
    InterfaceHandle<MediaRenderer> video_renderer,
    InterfaceRequest<MediaPlayer> player) {
  AddProduct(MediaPlayerImpl::Create(reader.Pass(), audio_renderer.Pass(),
                                     video_renderer.Pass(), player.Pass(),
                                     this));
}

void MediaFactoryService::CreateSource(InterfaceHandle<SeekingReader> reader,
                                       Array<MediaTypeSetPtr> media_types,
                                       InterfaceRequest<MediaSource> source) {
  AddProduct(
      MediaSourceImpl::Create(reader.Pass(), media_types, source.Pass(), this));
}

void MediaFactoryService::CreateSink(InterfaceHandle<MediaRenderer> renderer,
                                     MediaTypePtr media_type,
                                     InterfaceRequest<MediaSink> sink) {
  AddProduct(MediaSinkImpl::Create(renderer.Pass(), media_type.Pass(),
                                   sink.Pass(), this));
}

void MediaFactoryService::CreateDemux(InterfaceHandle<SeekingReader> reader,
                                      InterfaceRequest<MediaDemux> demux) {
  AddProduct(MediaDemuxImpl::Create(reader.Pass(), demux.Pass(), this));
}

void MediaFactoryService::CreateDecoder(
    MediaTypePtr input_media_type,
    InterfaceRequest<MediaTypeConverter> decoder) {
  AddProduct(
      MediaDecoderImpl::Create(input_media_type.Pass(), decoder.Pass(), this));
}

void MediaFactoryService::CreateNetworkReader(
    const String& url,
    InterfaceRequest<SeekingReader> reader) {
  AddProduct(NetworkReaderImpl::Create(url, reader.Pass(), this));
}

void MediaFactoryService::CreateTimelineController(
    InterfaceRequest<MediaTimelineController> timeline_controller) {
  AddProduct(
      MediaTimelineControllerImpl::Create(timeline_controller.Pass(), this));
}

}  // namespace media
}  // namespace mojo
