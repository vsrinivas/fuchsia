// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/app/cpp/application_context.h"
#include "lib/media/fidl/media_service.fidl.h"
#include "garnet/bin/media/util/factory_service_base.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

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
  MediaServiceImpl(
      std::unique_ptr<app::ApplicationContext> application_context);
  ~MediaServiceImpl() override;

  ftl::RefPtr<ftl::TaskRunner> multiproc_task_runner() {
    return multiproc_task_runner_;
  }

  // MediaService implementation.
  void CreatePlayer(fidl::InterfaceHandle<SeekingReader> reader,
                    fidl::InterfaceHandle<MediaRenderer> audio_renderer,
                    fidl::InterfaceHandle<MediaRenderer> video_renderer,
                    fidl::InterfaceRequest<MediaPlayer> player) override;

  void CreateSource(fidl::InterfaceHandle<SeekingReader> reader,
                    fidl::Array<MediaTypeSetPtr> allowed_media_types,
                    fidl::InterfaceRequest<MediaSource> source) override;

  void CreateSink(fidl::InterfaceHandle<MediaRenderer> renderer,
                  fidl::InterfaceRequest<MediaSink> sink_request) override;

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

 private:
  fidl::BindingSet<MediaService> bindings_;
  ftl::RefPtr<ftl::TaskRunner> multiproc_task_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MediaServiceImpl);
};

}  // namespace media
