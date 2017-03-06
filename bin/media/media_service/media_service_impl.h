// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "application/lib/app/application_context.h"
#include "apps/media/services/media_service.fidl.h"
#include "apps/media/src/util/factory_service_base.h"
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
                  fidl::InterfaceRequest<MediaSink> sink_request,
                  fidl::InterfaceRequest<MediaPacketConsumer>
                      packet_consumer_request) override;

  void CreateDemux(fidl::InterfaceHandle<SeekingReader> reader,
                   fidl::InterfaceRequest<MediaSource> demux) override;

  void CreateDecoder(
      MediaTypePtr input_media_type,
      fidl::InterfaceRequest<MediaTypeConverter> decoder) override;

  void CreateNetworkReader(
      const fidl::String& url,
      fidl::InterfaceRequest<SeekingReader> reader) override;

  void CreateFileReader(const fidl::String& path,
                        fidl::InterfaceRequest<SeekingReader> reader) override;

  void CreateAudioRenderer(
      fidl::InterfaceRequest<AudioRenderer> audio_renderer,
      fidl::InterfaceRequest<MediaRenderer> media_renderer) override;

  void CreateVideoRenderer(
      fidl::InterfaceRequest<VideoRenderer> video_renderer,
      fidl::InterfaceRequest<MediaRenderer> media_renderer) override;

  void CreateAudioCapturer(
      fidl::InterfaceRequest<MediaCapturer> media_capturer) override;

  void CreateTimelineController(fidl::InterfaceRequest<MediaTimelineController>
                                    timeline_controller) override;

  void CreateLpcmReformatter(
      MediaTypePtr input_media_type,
      AudioSampleFormat output_sample_format,
      fidl::InterfaceRequest<MediaTypeConverter> lpcm_reformatter) override;

  void CreatePlayerProxy(const fidl::String& device_name,
                         const fidl::String& service_name,
                         fidl::InterfaceRequest<MediaPlayer> player) override;

 private:
  fidl::BindingSet<MediaService> bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MediaServiceImpl);
};

}  // namespace media
