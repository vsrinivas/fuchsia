// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FACTORY_SERVICE_FACTORY_SERVICE_H_
#define SERVICES_MEDIA_FACTORY_SERVICE_FACTORY_SERVICE_H_

#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/services/media/control/interfaces/media_factory.mojom.h"
#include "services/util/cpp/factory_service_base.h"

namespace mojo {
namespace media {

class MediaFactoryService : public util::FactoryServiceBase,
                            public MediaFactory {
 public:
  MediaFactoryService();

  ~MediaFactoryService() override;

  // ApplicationImplBase override.
  void OnInitialize() override;

  bool OnAcceptConnection(ServiceProviderImpl* service_provider_impl) override;

  // MediaFactory implementation.
  void CreatePlayer(InterfaceHandle<SeekingReader> reader,
                    InterfaceHandle<MediaRenderer> audio_renderer,
                    InterfaceHandle<MediaRenderer> video_renderer,
                    InterfaceRequest<MediaPlayer> player) override;

  void CreateSource(InterfaceHandle<SeekingReader> reader,
                    Array<MediaTypeSetPtr> allowed_media_types,
                    InterfaceRequest<MediaSource> source) override;

  void CreateSink(InterfaceHandle<MediaRenderer> renderer,
                  MediaTypePtr media_type,
                  InterfaceRequest<MediaSink> sink) override;

  void CreateDemux(InterfaceHandle<SeekingReader> reader,
                   InterfaceRequest<MediaDemux> demux) override;

  void CreateDecoder(MediaTypePtr input_media_type,
                     InterfaceRequest<MediaTypeConverter> decoder) override;

  void CreateNetworkReader(const String& url,
                           InterfaceRequest<SeekingReader> reader) override;

  void CreateTimelineController(
      InterfaceRequest<MediaTimelineController> timeline_controller) override;

 private:
  BindingSet<MediaFactory> bindings_;
};

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FACTORY_SERVICE_FACTORY_SERVICE_H_
