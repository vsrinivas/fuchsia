// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_MEDIA_PLAYER_IMPL_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_MEDIA_PLAYER_IMPL_H_

#include <unordered_map>

#include <lib/async/default.h>
#include <media/cpp/fidl.h>

#include "garnet/bin/media/media_player/demux/reader.h"
#include "garnet/bin/media/media_player/player/player.h"
#include "garnet/bin/media/media_player/render/fidl_audio_renderer.h"
#include "garnet/bin/media/media_player/render/fidl_video_renderer.h"
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

  void SetGain(float gain) override;

  void CreateView(fidl::InterfaceHandle<::fuchsia::ui::views_v1::ViewManager> view_manager,
                  fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
                      view_owner_request) override;

  void SetAudioRenderer(
      fidl::InterfaceHandle<media::AudioRenderer2> audio_renderer) override;

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

  static const char* ToString(State value);

  // Begins the process of setting the reader.
  void BeginSetReader(std::shared_ptr<Reader> reader);

  // Finishes the process of setting the reader, assuming we're in |kIdle|
  // state and have no source segment.
  void FinishSetReader();

  // Creates the renderer for |medium| if it doesn't exist already.
  void MaybeCreateRenderer(StreamType::Medium medium);

  // Creates sinks as needed and connects enabled streams.
  void ConnectSinks();

  // Prepares a stream.
  void PrepareStream(size_t index, const media::MediaType& input_media_type,
                     const std::function<void()>& callback);

  // Takes action based on current state.
  void Update();

  // Determines whether we need to flush.
  bool NeedToFlush() const {
    return setting_reader_ || target_position_ != media::kUnspecifiedTime ||
           target_state_ == State::kFlushed;
  }

  // Determines whether we should hold a frame when flushing.
  bool ShouldHoldFrame() const {
    return !setting_reader_ && target_state_ != State::kFlushed;
  }

  // Sets the timeline function.
  void SetTimelineFunction(float rate, int64_t reference_time,
                           fxl::Closure callback);

  // Sends status updates to clients.
  void SendStatusUpdates();

  // Updates |status_|.
  void UpdateStatus();

  async_t* async_;
  component::ApplicationContext* application_context_;
  fxl::Closure quit_callback_;
  fidl::BindingSet<MediaPlayer> bindings_;
  Player player_;

  float gain_ = 1.0f;
  std::shared_ptr<FidlAudioRenderer> audio_renderer_;
  std::shared_ptr<FidlVideoRenderer> video_renderer_;

  // The state we're currently in.
  State state_ = State::kWaiting;
  const char* waiting_reason_ = "to initialize";

  // The state we're trying to transition to, either because the client has
  // called |Play| or |Pause| or because we've hit end-of-stream.
  State target_state_ = State::kFlushed;

  // The position we want to seek to (because the client called Seek) or
  // media::kUnspecifiedTime, which indicates there's no desire to seek.
  int64_t target_position_ = media::kUnspecifiedTime;

  // The subject time to be used for SetTimelineFunction. The value is
  // media::kUnspecifiedTime if there's no need to seek or the position we want
  // to seek to if there is.
  int64_t transform_subject_time_ = media::kUnspecifiedTime;

  // The minimum program range PTS to be used for SetProgramRange.
  int64_t program_range_min_pts_ = media::kMinTime;

  // Whether we need to set the reader, possibly with nothing. When this is
  // true, the state machine will transition to |kIdle|, removing an existing
  // reader if there is one, then call |FinishSetReader| to set up the new
  // reader |new_reader_|.
  bool setting_reader_ = false;

  // Reader that needs to be used once we're ready to use it. If this field is
  // null when |setting_reader_| is true, we're waiting to remove the existing
  // reader and transition to kInactive.
  std::shared_ptr<Reader> new_reader_;

  MediaPlayerStatus status_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_MEDIA_PLAYER_IMPL_H_
