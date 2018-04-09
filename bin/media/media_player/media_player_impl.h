// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_MEDIA_PLAYER_IMPL_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_MEDIA_PLAYER_IMPL_H_

#include <unordered_map>

#include <fuchsia/cpp/media.h>

#include "garnet/bin/media/demux/reader.h"
#include "garnet/bin/media/player/player.h"
#include "garnet/bin/media/render/fidl_audio_renderer.h"
#include "garnet/bin/media/render/fidl_video_renderer.h"
#include "garnet/bin/media/util/fidl_publisher.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/functional/closure.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_function.h"

namespace media_player {

// Fidl agent that renders streams.
class MediaPlayerImpl : public MediaPlayer {
 public:
  static std::unique_ptr<MediaPlayerImpl> Create(
      fidl::InterfaceRequest<MediaPlayer> request,
      component::ApplicationContext* application_context,
      fxl::Closure quit_callback);

  MediaPlayerImpl(fidl::InterfaceRequest<MediaPlayer> request,
                  component::ApplicationContext* application_context,
                  fxl::Closure quit_callback);

  ~MediaPlayerImpl() override;

  // MediaPlayer implementation.
  void SetHttpSource(fidl::StringPtr http_url) override;

  void SetFileSource(zx::channel file_channel) override;

  void SetReaderSource(
      fidl::InterfaceHandle<SeekingReader> reader_handle) override;

  void Play() override;

  void Pause() override;

  void Seek(int64_t position) override;

  void GetStatus(uint64_t version_last_seen,
                 GetStatusCallback callback) override;

  void SetGain(float gain) override;

  void CreateView(fidl::InterfaceHandle<views_v1::ViewManager> view_manager,
                  fidl::InterfaceRequest<views_v1_token::ViewOwner>
                      view_owner_request) override;

  void SetAudioRenderer(
      fidl::InterfaceHandle<media::AudioRenderer> audio_renderer,
      fidl::InterfaceHandle<media::MediaRenderer> media_renderer) override;

  void AddBinding(fidl::InterfaceRequest<MediaPlayer> request) override;

 private:
  static constexpr int64_t kMinimumLeadTime = media::Timeline::ns_from_ms(30);

  // Internal state.
  enum class State {
    kInactive,  // Waiting for a reader to be supplied.
    kWaiting,   // Waiting for some work to complete.
    kFlushed,   // Paused with no data in the pipeline.
    kPrimed,    // Paused with data in the pipeline.
    kPlaying,   // Time is progressing.
  };

  // Sets the current reader.
  void SetReader(std::shared_ptr<Reader> reader);

  // Creates the renderer for |medium| if it doesn't exist already.
  void MaybeCreateRenderer(media::StreamType::Medium medium);

  // Creates sinks as needed and connects enabled streams.
  void ConnectSinks();

  // Prepares a stream.
  void PrepareStream(size_t index,
                     const media::MediaType& input_media_type,
                     const std::function<void()>& callback);

  // Takes action based on current state.
  void Update();

  // Sets the timeline function.
  void SetTimelineFunction(float rate,
                           int64_t reference_time,
                           fxl::Closure callback);

  component::ApplicationContext* application_context_;
  fxl::Closure quit_callback_;
  fidl::BindingSet<MediaPlayer> bindings_;
  Player player_;

  float gain_ = 1.0f;
  std::shared_ptr<FidlAudioRenderer> audio_renderer_;
  std::shared_ptr<FidlVideoRenderer> video_renderer_;

  // The state we're currently in.
  State state_ = State::kWaiting;

  // The state we're trying to transition to, either because the client has
  // called |Play| or |Pause| or because we've hit end-of-stream.
  State target_state_ = State::kFlushed;

  // The position we want to seek to (because the client called Seek) or
  // kUnspecifiedTime, which indicates there's no desire to seek.
  int64_t target_position_ = media::kUnspecifiedTime;

  // The subject time to be used for SetTimelineFunction. The value is
  // kUnspecifiedTime if there's no need to seek or the position we want to
  // seek to if there is.
  int64_t transform_subject_time_ = media::kUnspecifiedTime;

  // The minimum program range PTS to be used for SetProgramRange.
  int64_t program_range_min_pts_ = media::kMinTime;

  media::FidlPublisher<GetStatusCallback> status_publisher_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_MEDIA_PLAYER_IMPL_H_
