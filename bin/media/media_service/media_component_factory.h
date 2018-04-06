// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/media.h>

#include "garnet/bin/media/util/factory_service_base.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"

namespace media {

class VideoRendererImpl;

class MediaComponentFactory : public FactoryServiceBase<MediaComponentFactory> {
 public:
  MediaComponentFactory(
      std::unique_ptr<component::ApplicationContext> application_context,
      fxl::Closure quit_callback);

  ~MediaComponentFactory() override;

  void CreateMediaPlayer(fidl::InterfaceRequest<MediaPlayer> player);

  void CreateSource(fidl::InterfaceHandle<SeekingReader> reader,
                    fidl::VectorPtr<MediaTypeSet> allowed_media_types,
                    fidl::InterfaceRequest<MediaSource> source);

  void CreateSink(fidl::InterfaceHandle<MediaRenderer> renderer,
                  fidl::InterfaceRequest<MediaSink> sink_request);

  void CreateDemux(fidl::InterfaceHandle<SeekingReader> reader,
                   fidl::InterfaceRequest<MediaSource> demux);

  void CreateDecoder(MediaTypePtr input_media_type,
                     fidl::InterfaceRequest<MediaTypeConverter> decoder);

  void CreateTimelineController(
      fidl::InterfaceRequest<MediaTimelineController> timeline_controller);

  void CreateLpcmReformatter(
      MediaTypePtr input_media_type,
      AudioSampleFormat output_sample_format,
      fidl::InterfaceRequest<MediaTypeConverter> lpcm_reformatter);

  // Creates a |SeekingReader| that reads from an HTTP service.
  void CreateHttpReader(const std::string& http_url,
                        fidl::InterfaceRequest<SeekingReader> reader);

  // Creates a |SeekingReader| that reads from an fdio channel for a file.
  void CreateFileChannelReader(zx::channel file_channel,
                               fidl::InterfaceRequest<SeekingReader> reader);

  // Creates a video renderer.
  std::shared_ptr<VideoRendererImpl> CreateVideoRenderer(
      fidl::InterfaceRequest<MediaRenderer> media_renderer);

 private:
  // FactoryServiceBase override.
  void OnLastProductRemoved() override;

  fxl::Closure quit_callback_;
  async_t* async_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaComponentFactory);
};

}  // namespace media
