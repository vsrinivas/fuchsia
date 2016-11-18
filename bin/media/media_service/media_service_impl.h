// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/services/media_service.fidl.h"
#include "apps/media/src/util/factory_service_base.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

namespace media {

class MediaServiceImpl : public FactoryServiceBase, public MediaService {
 public:
  MediaServiceImpl();

  ~MediaServiceImpl() override;

  // MediaService implementation.
  void CreatePlayer(fidl::InterfaceHandle<SeekingReader> reader,
                    fidl::InterfaceHandle<MediaRenderer> audio_renderer,
                    fidl::InterfaceHandle<MediaRenderer> video_renderer,
                    fidl::InterfaceRequest<MediaPlayer> player) override;

  void CreateSource(fidl::InterfaceHandle<SeekingReader> reader,
                    fidl::Array<MediaTypeSetPtr> allowed_media_types,
                    fidl::InterfaceRequest<MediaSource> source) override;

  void CreateSink(fidl::InterfaceHandle<MediaRenderer> renderer,
                  MediaTypePtr media_type,
                  fidl::InterfaceRequest<MediaSink> sink) override;

  void CreateDemux(fidl::InterfaceHandle<SeekingReader> reader,
                   fidl::InterfaceRequest<MediaDemux> demux) override;

  void CreateDecoder(
      MediaTypePtr input_media_type,
      fidl::InterfaceRequest<MediaTypeConverter> decoder) override;

  void CreateNetworkReader(
      const fidl::String& url,
      fidl::InterfaceRequest<SeekingReader> reader) override;

  void CreateFileReader(const fidl::String& path,
                        fidl::InterfaceRequest<SeekingReader> reader) override;

  void CreateVideoRenderer(
      fidl::InterfaceRequest<VideoRenderer> video_renderer,
      fidl::InterfaceRequest<MediaRenderer> media_renderer) override;

  void CreateTimelineController(fidl::InterfaceRequest<MediaTimelineController>
                                    timeline_controller) override;

 private:
  fidl::BindingSet<MediaService> bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MediaServiceImpl);
};

}  // namespace media
