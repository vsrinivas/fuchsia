// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_PLAYER_IMPL_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_PLAYER_IMPL_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/media/cpp/timeline_function.h>
#include <lib/sys/cpp/component_context.h>

#include <unordered_map>

#include "src/media/playback/mediaplayer/core/player_core.h"
#include "src/media/playback/mediaplayer/demux/demux.h"
#include "src/media/playback/mediaplayer/demux/reader.h"
#include "src/media/playback/mediaplayer/fidl/fidl_audio_renderer.h"
#include "src/media/playback/mediaplayer/fidl/fidl_video_renderer.h"
#include "src/media/playback/mediaplayer/graph/service_provider.h"
#include "src/media/playback/mediaplayer/process/processor.h"
#include "src/media/playback/mediaplayer/source_impl.h"

namespace media_player {

static_assert(fuchsia::media::NO_TIMESTAMP == Packet::kNoPts);

// Fidl agent that renders streams.
class PlayerImpl : public fuchsia::media::playback::Player, public ServiceProvider {
 public:
  static std::unique_ptr<PlayerImpl> Create(
      fidl::InterfaceRequest<fuchsia::media::playback::Player> request,
      sys::ComponentContext* component_context, fit::closure quit_callback);

  PlayerImpl(fidl::InterfaceRequest<fuchsia::media::playback::Player> request,
             sys::ComponentContext* component_context, fit::closure quit_callback);

  ~PlayerImpl() override;

  void SetFileSource(zx::channel file_channel) override;

  void Play() override;

  void Pause() override;

  void Seek(int64_t position) override;

  void CreateView(fuchsia::ui::views::ViewToken view_token) override;

  void BindGainControl(
      fidl::InterfaceRequest<fuchsia::media::audio::GainControl> gain_control_request) override;

  void AddBinding(fidl::InterfaceRequest<fuchsia::media::playback::Player> request) override;

  void CreateFileSource(
      zx::channel file_channel,
      fidl::InterfaceRequest<fuchsia::media::playback::Source> source_request) override;

  void CreateReaderSource(
      fidl::InterfaceHandle<fuchsia::media::playback::SeekingReader> seeking_reader,
      fidl::InterfaceRequest<fuchsia::media::playback::Source> source_request) override;

  void CreateElementarySource(
      int64_t duration_ns, bool can_pause, bool can_seek,
      std::unique_ptr<fuchsia::media::Metadata> metadata,
      fidl::InterfaceRequest<fuchsia::media::playback::ElementarySource> source_request) override;

  void SetSource(fidl::InterfaceHandle<fuchsia::media::playback::Source> source) override;

  void TransitionToSource(fidl::InterfaceHandle<fuchsia::media::playback::Source> source,
                          int64_t transition_pts, int64_t start_pts) override;

  void CancelSourceTransition(
      fidl::InterfaceRequest<fuchsia::media::playback::Source> returned_source_request) override;

  // ServiceProvider implementation.
  void ConnectToService(std::string service_path, zx::channel channel) override;

 private:
  static constexpr int64_t kMinimumLeadTime = ZX_MSEC(30);

  // Internal state.
  enum class State {
    kInactive,  // Waiting for a reader to be supplied.
    kWaiting,   // Waiting for some work to complete.
    kFlushed,   // Paused with no data in the pipeline.
    kPrimed,    // Paused with data in the pipeline.
    kPlaying,   // Time is progressing.
  };

  static const char* ToString(State value);

  // Adds a binding to |bindings_| and fires the |OnStatusChanged| for the new
  // binding.
  void AddBindingInternal(fidl::InterfaceRequest<fuchsia::media::playback::Player> request);

  // Begins the process of setting a new source.
  void BeginSetSource(std::unique_ptr<SourceImpl> source);

  // Finishes the process of setting a new source, assuming we're in |kIdle|
  // state and have no current source.
  void FinishSetSource();

  // Creates the renderer for |medium| if it doesn't exist already.
  void MaybeCreateRenderer(StreamType::Medium medium);

  // Creates sinks as needed and connects enabled streams.
  void ConnectSinks();

  // Takes action based on current state.
  void Update();

  // Determines whether we need to flush.
  bool NeedToFlush() const {
    return setting_source_ || target_position_ != Packet::kNoPts ||
           target_state_ == State::kFlushed;
  }

  // Determines whether we should hold a frame when flushing.
  bool ShouldHoldFrame() const { return !setting_source_ && target_state_ != State::kFlushed; }

  // Sets the timeline function.
  void SetTimelineFunction(float rate, int64_t reference_time, fit::closure callback);

  // Creates a |Source| that uses the specified reader. |source_request| is
  // optional. The optional |connection_failure_callback| is provided to the
  // source to signal a connection failure.
  std::unique_ptr<SourceImpl> CreateSource(
      std::shared_ptr<Reader> reader,
      fidl::InterfaceRequest<fuchsia::media::playback::Source> source_request,
      fit::closure connection_failure_callback = nullptr);

  // Sends status updates to clients.
  void SendStatusUpdates();

  // Updates |status_|.
  void UpdateStatus();

  async_dispatcher_t* dispatcher_;
  sys::ComponentContext* component_context_;
  fit::closure quit_callback_;
  fidl::BindingSet<fuchsia::media::playback::Player> bindings_;
  PlayerCore core_;
  std::unique_ptr<DemuxFactory> demux_factory_;
  std::unique_ptr<DecoderFactory> decoder_factory_;

  std::shared_ptr<FidlAudioRenderer> audio_renderer_;
  std::shared_ptr<FidlVideoRenderer> video_renderer_;

  // The state we're currently in.
  State state_ = State::kWaiting;
  const char* waiting_reason_ = "to initialize";

  // Indicates that the player has become ready after the source has been set.
  // The actual ready value reported in status is true if and only if this
  // field is true and there is no problem.
  bool ready_if_no_problem_ = false;

  // The state we're trying to transition to, either because the client has
  // called |Play| or |Pause| or because we've hit end-of-stream.
  State target_state_ = State::kFlushed;

  // The position we want to seek to (because the client called Seek) or
  // kUnspecifiedTime, which indicates there's no desire to seek.
  int64_t target_position_ = Packet::kNoPts;

  // The subject time to be used for SetTimelineFunction. The value is
  // kUnspecifiedTime if there's no need to seek or the position we want
  // to seek to if there is.
  int64_t transform_subject_time_ = Packet::kNoPts;

  // The minimum program range PTS to be used for SetProgramRange.
  int64_t program_range_min_pts_ = Packet::kMinPts;

  // Whether the player is in the process of setting the source, possibly to
  // nothing. This is set to true when any of the Set*Source methods is called,
  // at which time |new_source_| is set to identify the new source. In this
  // state, the state machine will transition to |kIdle|, removing an existing
  // source, if there is one, then call |FinishSetSource| to set up the new
  // source.
  bool setting_source_ = false;

  // |SourceImpl| that needs to be used once we're ready to use it. If this
  // field is null when |setting_source_| is true, we're waiting to remove the
  // existing source and transition to kInactive.
  std::unique_ptr<SourceImpl> new_source_;

  // Handle for |new_source_| passed to |SetSource|. We keep this around in
  // case there are messages in the channel that need to be processed.
  fidl::InterfaceHandle<fuchsia::media::playback::Source> new_source_handle_;

  // |SourceImpl| that wrapped the |SourceSegment| currently in use by |core_|
  // and the corresponding handle.
  std::unique_ptr<SourceImpl> current_source_;
  fidl::InterfaceHandle<fuchsia::media::playback::Source> current_source_handle_;

  // Stores all the sources that have been created and not destroyed or set
  // on the player via |SetSource| (which, actually, destroys the |SourceImpl|).
  std::unordered_map<zx_koid_t, std::unique_ptr<SourceImpl>> source_impls_by_koid_;

  // Current status.
  fuchsia::media::playback::PlayerStatus status_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_PLAYER_IMPL_H_
