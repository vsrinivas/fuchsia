// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_AUDIO_CONSUMER_IMPL_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_AUDIO_CONSUMER_IMPL_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/vmo.h>

#include <memory>

#include "lib/fidl/cpp/interface_request.h"
#include "lib/vfs/cpp/service.h"
#include "src/media/playback/mediaplayer/core/player_core.h"
#include "src/media/playback/mediaplayer/demux/demux.h"
#include "src/media/playback/mediaplayer/demux/reader.h"
#include "src/media/playback/mediaplayer/fidl/fidl_audio_renderer.h"
#include "src/media/playback/mediaplayer/graph/service_provider.h"
#include "src/media/playback/mediaplayer/process/processor.h"
#include "src/media/playback/mediaplayer/source_impl.h"

namespace media_player {

// Fidl service that gives out StreamSinks.
class AudioConsumerImpl : public fuchsia::media::AudioConsumer, public ServiceProvider {
 public:
  static std::unique_ptr<AudioConsumerImpl> Create(
      fidl::InterfaceRequest<fuchsia::media::AudioConsumer> request,
      sys::ComponentContext* component_context, fit::closure quit_callback);

  AudioConsumerImpl(fidl::InterfaceRequest<fuchsia::media::AudioConsumer> request,
                    sys::ComponentContext* component_context, fit::closure quit_callback);

  ~AudioConsumerImpl() override;

  // AudioConsumer implementation.
  void CreateStreamSink(
      uint64_t session_id, std::vector<zx::vmo> buffers,
      fuchsia::media::AudioStreamType stream_type,
      std::unique_ptr<fuchsia::media::Compression> compression,
      fidl::InterfaceRequest<fuchsia::media::StreamSink> stream_sink_request) override;

  void Start(fuchsia::media::AudioConsumerStartFlags flags, int64_t reference_time,
             int64_t media_time) override;

  void Stop() override;

  void WatchStatus(fuchsia::media::AudioConsumer::WatchStatusCallback callback) override;

  void SetRate(float rate) override;

  void BindVolumeControl(
      fidl::InterfaceRequest<fuchsia::media::audio::VolumeControl> request) override;

  // ServiceProvider implementation.
  void ConnectToService(std::string service_path, zx::channel channel) override;

 private:
  static constexpr int64_t kMinimumLeadTime = ZX_MSEC(30);
  static constexpr int64_t kMaximumLeadTime = ZX_MSEC(500);

  // Adds a binding to |bindings_|
  void AddBindingInternal(fidl::InterfaceRequest<fuchsia::media::AudioConsumer> request);

  // Ensures renderer is created
  void EnsureRenderer();

  // Clears out any existing source segment in the player, and sets up any pending new one
  void MaybeSetNewSource();

  // Sets the timeline function.
  void SetTimelineFunction(float rate, int64_t subject_time, int64_t reference_time,
                           fit::closure callback);

  void SendStatusUpdate();

  async_dispatcher_t* dispatcher_;
  sys::ComponentContext* component_context_;
  fit::closure quit_callback_;
  PlayerCore core_;
  fidl::BindingSet<fuchsia::media::AudioConsumer> bindings_;
  std::unique_ptr<DecoderFactory> decoder_factory_;

  // New stream sink requested by client, but not added to player yet
  std::shared_ptr<SimpleStreamSinkImpl> pending_simple_stream_sink_;
  std::vector<zx::vmo> pending_buffers_;

  fuchsia::media::AudioConsumer::WatchStatusCallback watch_status_callback_;

  std::shared_ptr<FidlAudioRenderer> audio_renderer_;

  bool timeline_started_;
  bool status_dirty_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_AUDIO_CONSUMER_IMPL_H_
