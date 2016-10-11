// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/media_service_impl.h"

#include "apps/media/cpp/flog.h"
#include "apps/media/src/media_service/media_decoder_impl.h"
#include "apps/media/src/media_service/media_demux_impl.h"
#include "apps/media/src/media_service/media_player_impl.h"
#include "apps/media/src/media_service/media_sink_impl.h"
#include "apps/media/src/media_service/media_source_impl.h"
#include "apps/media/src/media_service/media_timeline_controller_impl.h"
//#include "apps/media/src/media_service/network_reader_impl.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace mojo {
namespace media {

MediaServiceImpl::MediaServiceImpl() {}

MediaServiceImpl::~MediaServiceImpl() {
  FLOG_DESTROY();
}

void MediaServiceImpl::OnInitialize() {
  FLOG_INITIALIZE(shell(), "media_service");
}

bool MediaServiceImpl::OnAcceptConnection(
    ServiceProviderImpl* service_provider_impl) {
  service_provider_impl->AddService<MediaService>(
      [this](const ConnectionContext& connection_context,
             InterfaceRequest<MediaService> media_service_request) {
        bindings_.AddBinding(this, media_service_request.Pass());
      });
  return true;
}

void MediaServiceImpl::CreatePlayer(
    InterfaceHandle<SeekingReader> reader,
    InterfaceHandle<MediaRenderer> audio_renderer,
    InterfaceHandle<MediaRenderer> video_renderer,
    InterfaceRequest<MediaPlayer> player) {
  AddProduct(MediaPlayerImpl::Create(reader.Pass(), audio_renderer.Pass(),
                                     video_renderer.Pass(), player.Pass(),
                                     this));
}

void MediaServiceImpl::CreateSource(InterfaceHandle<SeekingReader> reader,
                                    Array<MediaTypeSetPtr> media_types,
                                    InterfaceRequest<MediaSource> source) {
  AddProduct(
      MediaSourceImpl::Create(reader.Pass(), media_types, source.Pass(), this));
}

void MediaServiceImpl::CreateSink(InterfaceHandle<MediaRenderer> renderer,
                                  MediaTypePtr media_type,
                                  InterfaceRequest<MediaSink> sink) {
  AddProduct(MediaSinkImpl::Create(renderer.Pass(), media_type.Pass(),
                                   sink.Pass(), this));
}

void MediaServiceImpl::CreateDemux(InterfaceHandle<SeekingReader> reader,
                                   InterfaceRequest<MediaDemux> demux) {
  AddProduct(MediaDemuxImpl::Create(reader.Pass(), demux.Pass(), this));
}

void MediaServiceImpl::CreateDecoder(
    MediaTypePtr input_media_type,
    InterfaceRequest<MediaTypeConverter> decoder) {
  AddProduct(
      MediaDecoderImpl::Create(input_media_type.Pass(), decoder.Pass(), this));
}

void MediaServiceImpl::CreateNetworkReader(
    const String& url,
    InterfaceRequest<SeekingReader> reader) {
  FTL_DCHECK(false) << "CreateNetworkReader not implemented";
  // AddProduct(NetworkReaderImpl::Create(url, reader.Pass(), this));
}

void MediaServiceImpl::CreateTimelineController(
    InterfaceRequest<MediaTimelineController> timeline_controller) {
  AddProduct(
      MediaTimelineControllerImpl::Create(timeline_controller.Pass(), this));
}

}  // namespace media
}  // namespace mojo
