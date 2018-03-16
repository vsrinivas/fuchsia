// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/util/factory_service_base.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/macros.h"
#include "lib/media/fidl/media_player.fidl.h"
#include "lib/media/fidl/media_renderer.fidl.h"
#include "lib/media/fidl/media_sink.fidl.h"
#include "lib/media/fidl/media_source.fidl.h"
#include "lib/media/fidl/media_type_converter.fidl.h"
#include "lib/media/fidl/seeking_reader.fidl.h"
#include "lib/media/fidl/video_renderer.fidl.h"

namespace media {

class VideoRendererImpl;

class MediaComponentFactory : public FactoryServiceBase<MediaComponentFactory> {
 public:
  MediaComponentFactory(
      std::unique_ptr<component::ApplicationContext> application_context);

  ~MediaComponentFactory() override;

  fxl::RefPtr<fxl::TaskRunner> multiproc_task_runner() {
    return multiproc_task_runner_;
  }

  void CreateMediaPlayer(f1dl::InterfaceRequest<MediaPlayer> player);

  void CreateSource(f1dl::InterfaceHandle<SeekingReader> reader,
                    f1dl::Array<MediaTypeSetPtr> allowed_media_types,
                    f1dl::InterfaceRequest<MediaSource> source);

  void CreateSink(f1dl::InterfaceHandle<MediaRenderer> renderer,
                  f1dl::InterfaceRequest<MediaSink> sink_request);

  void CreateDemux(f1dl::InterfaceHandle<SeekingReader> reader,
                   f1dl::InterfaceRequest<MediaSource> demux);

  void CreateDecoder(MediaTypePtr input_media_type,
                     f1dl::InterfaceRequest<MediaTypeConverter> decoder);

  void CreateTimelineController(
      f1dl::InterfaceRequest<MediaTimelineController> timeline_controller);

  void CreateLpcmReformatter(
      MediaTypePtr input_media_type,
      AudioSampleFormat output_sample_format,
      f1dl::InterfaceRequest<MediaTypeConverter> lpcm_reformatter);

  // Creates a |SeekingReader| that reads from an HTTP service.
  void CreateHttpReader(const std::string& http_url,
                        f1dl::InterfaceRequest<SeekingReader> reader);

  // Creates a |SeekingReader| that reads from an fdio channel for a file.
  void CreateFileChannelReader(zx::channel file_channel,
                               f1dl::InterfaceRequest<SeekingReader> reader);

  // Creates a video renderer.
  std::shared_ptr<VideoRendererImpl> CreateVideoRenderer(
      f1dl::InterfaceRequest<MediaRenderer> media_renderer);

 private:
  // FactoryServiceBase override.
  void OnLastProductRemoved() override;

  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  fxl::RefPtr<fxl::TaskRunner> multiproc_task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaComponentFactory);
};

}  // namespace media
