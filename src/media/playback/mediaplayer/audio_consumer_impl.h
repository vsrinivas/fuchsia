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
#include <unordered_set>

#include "lib/fidl/cpp/interface_request.h"
#include "lib/media/cpp/timeline_rate.h"
#include "lib/vfs/cpp/service.h"
#include "src/media/playback/mediaplayer/core/player_core.h"
#include "src/media/playback/mediaplayer/demux/demux.h"
#include "src/media/playback/mediaplayer/demux/reader.h"
#include "src/media/playback/mediaplayer/fidl/fidl_audio_renderer.h"
#include "src/media/playback/mediaplayer/fidl/simple_stream_sink_impl.h"
#include "src/media/playback/mediaplayer/graph/service_provider.h"
#include "src/media/playback/mediaplayer/process/processor.h"
#include "src/media/playback/mediaplayer/source_impl.h"

namespace media_player {

class AudioConsumerImpl;

// Fidl service that gives out AudioConsumer's bound to a particular session id.
class SessionAudioConsumerFactoryImpl : public fuchsia::media::SessionAudioConsumerFactory {
 public:
  static std::unique_ptr<SessionAudioConsumerFactoryImpl> Create(
      fidl::InterfaceRequest<fuchsia::media::SessionAudioConsumerFactory> request,
      sys::ComponentContext* component_context, fit::closure quit_callback);

  SessionAudioConsumerFactoryImpl(
      fidl::InterfaceRequest<fuchsia::media::SessionAudioConsumerFactory> request,
      sys::ComponentContext* component_context, fit::closure quit_callback);

  // SessionAudioConsumerFactory implementation.
  void CreateAudioConsumer(
      uint64_t session_id,
      fidl::InterfaceRequest<fuchsia::media::AudioConsumer> audio_consumer_request) override;

 private:
  // Call quit_callback_ if audio_consumers_ set is empty
  void MaybeQuit();

  std::unordered_set<std::shared_ptr<AudioConsumerImpl>> audio_consumers_;
  sys::ComponentContext* component_context_;
  fit::closure quit_callback_;
  fidl::Binding<fuchsia::media::SessionAudioConsumerFactory> binding_;
};

// Fidl service that gives out StreamSinks.
class AudioConsumerImpl : public fuchsia::media::AudioConsumer,
                          public ServiceProvider,
                          public std::enable_shared_from_this<AudioConsumerImpl> {
 public:
  static std::shared_ptr<AudioConsumerImpl> Create(
      uint64_t session_id, fidl::InterfaceRequest<fuchsia::media::AudioConsumer> request,
      sys::ComponentContext* component_context);

  AudioConsumerImpl(uint64_t session_id,
                    fidl::InterfaceRequest<fuchsia::media::AudioConsumer> request,
                    sys::ComponentContext* component_context);

  ~AudioConsumerImpl() override;

  // AudioConsumer implementation.
  void CreateStreamSink(
      std::vector<zx::vmo> buffers, fuchsia::media::AudioStreamType stream_type,
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

  // Set callback to be called when binding closes
  void SetQuitCallback(fit::closure quit_callback) { quit_callback_ = std::move(quit_callback); }

 private:
  static constexpr int64_t kMinimumLeadTime = ZX_MSEC(30);
  static constexpr int64_t kMaximumLeadTime = ZX_MSEC(500);
  static constexpr float kDefaultRate = 1.0f;

  // Ensures renderer is created
  void EnsureRenderer();

  // Clears out any existing source segment in the player, and sets up any pending new one
  void MaybeSetNewSource();

  // Sets the timeline function.
  void SetTimelineFunction(float rate, int64_t subject_time, int64_t reference_time,
                           fit::closure callback);

  // Calls |watch_status_callback_| with current status if present
  void SendStatusUpdate();

  // Callback handler for |core_| status updates
  void HandlePlayerStatusUpdate();

  // Callback handler for after |core_| is done updating timeline
  void OnTimelineUpdated(float rate);

  fidl::Binding<fuchsia::media::AudioConsumer> binding_;
  fit::closure quit_callback_;

  async_dispatcher_t* dispatcher_;
  sys::ComponentContext* component_context_;
  PlayerCore core_;
  std::unique_ptr<DecoderFactory> decoder_factory_;

  // New stream sink requested by client, but not added to player yet
  std::shared_ptr<SimpleStreamSinkImpl> pending_simple_stream_sink_;
  std::vector<zx::vmo> pending_buffers_;

  fuchsia::media::AudioConsumer::WatchStatusCallback watch_status_callback_;

  std::shared_ptr<FidlAudioRenderer> audio_renderer_;
  bool renderer_primed_;

  std::shared_ptr<SimpleStreamSinkImpl> simple_stream_sink_;

  bool timeline_started_;
  float rate_;
  bool status_dirty_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_AUDIO_CONSUMER_IMPL_H_
