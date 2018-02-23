// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/util/factory_service_base.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/media/fidl/media_service.fidl.h"

namespace media {

// Main media service implementation.
//
// |MediaServiceImpl| is a factory for various FIDL media components. Currently,
// all such components, other than audio renderers, are instantiated in the
// process in which the singleton instance of this class runs. This will change
// in the future so that potentially vulnerable components (e.g. decoders) are
// isolated with minimal privileges and no ability to interfere with components
// used by other clients.
//
// FIDL requires that a given interface implementation commit to a particular
// thread on which all messages are received and transmitted. The media
// components created by this class typically operate only on their designated
// FIDL message thread. For this reason, performance-critical components are
// instantiated on their own threads, allowing them to run concurrently with
// respect to other such components.
//
// The current assumption is that performance-critical components are those
// components that are actually in the media pipeline. This includes any
// component that produces or consumes packets as well as the readers that
// deliver raw data to the demultiplexer. Other components are instantiated on
// the same thread as the |MediaServiceImpl| instance.
class MediaServiceImpl : public FactoryServiceBase<MediaServiceImpl>,
                         public MediaService {
 public:
  static const std::string kIsolateUrl;
  static const std::string kIsolateArgument;

  MediaServiceImpl(std::unique_ptr<app::ApplicationContext> application_context,
                   bool transient);
  ~MediaServiceImpl() override;

  fxl::RefPtr<fxl::TaskRunner> multiproc_task_runner() {
    return multiproc_task_runner_;
  }

  // MediaService implementation.
  void CreateFilePlayer(
      zx::channel file_channel,
      f1dl::InterfaceHandle<media::MediaRenderer> audio_renderer,
      f1dl::InterfaceHandle<media::MediaRenderer> video_renderer,
      f1dl::InterfaceRequest<media::MediaPlayer> player) override;

  void CreateHttpPlayer(
      const f1dl::String& http_url,
      f1dl::InterfaceHandle<media::MediaRenderer> audio_renderer,
      f1dl::InterfaceHandle<media::MediaRenderer> video_renderer,
      f1dl::InterfaceRequest<media::MediaPlayer> player) override;

  void CreatePlayer(f1dl::InterfaceHandle<SeekingReader> reader,
                    f1dl::InterfaceHandle<MediaRenderer> audio_renderer,
                    f1dl::InterfaceHandle<MediaRenderer> video_renderer,
                    f1dl::InterfaceRequest<MediaPlayer> player) override;

  void CreateSource(f1dl::InterfaceHandle<SeekingReader> reader,
                    f1dl::Array<MediaTypeSetPtr> allowed_media_types,
                    f1dl::InterfaceRequest<MediaSource> source) override;

  void CreateSink(f1dl::InterfaceHandle<MediaRenderer> renderer,
                  f1dl::InterfaceRequest<MediaSink> sink_request) override;

  void CreateDemux(f1dl::InterfaceHandle<SeekingReader> reader,
                   f1dl::InterfaceRequest<MediaSource> demux) override;

  void CreateDecoder(
      MediaTypePtr input_media_type,
      f1dl::InterfaceRequest<MediaTypeConverter> decoder) override;

  void CreateAudioRenderer(
      f1dl::InterfaceRequest<AudioRenderer> audio_renderer,
      f1dl::InterfaceRequest<MediaRenderer> media_renderer) override;

  void CreateVideoRenderer(
      f1dl::InterfaceRequest<VideoRenderer> video_renderer,
      f1dl::InterfaceRequest<MediaRenderer> media_renderer) override;

  void CreateTimelineController(f1dl::InterfaceRequest<MediaTimelineController>
                                    timeline_controller) override;

  void CreateLpcmReformatter(
      MediaTypePtr input_media_type,
      AudioSampleFormat output_sample_format,
      f1dl::InterfaceRequest<MediaTypeConverter> lpcm_reformatter) override;

  // Creates a |SeekingReader| that reads from an HTTP service.
  void CreateHttpReader(const std::string& http_url,
                        f1dl::InterfaceRequest<SeekingReader> reader);

  // Creates a |SeekingReader| that reads from an fdio channel for a file.
  void CreateFileChannelReader(zx::channel file_channel,
                               f1dl::InterfaceRequest<SeekingReader> reader);

 private:
  // FactoryServiceBase override.
  void OnLastProductRemoved() override;

  // Creates a new transient media_service process.
  MediaServicePtr CreateIsolate();

  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  f1dl::BindingSet<MediaService> bindings_;
  fxl::RefPtr<fxl::TaskRunner> multiproc_task_runner_;
  app::ApplicationLauncherPtr launcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaServiceImpl);
};

}  // namespace media
