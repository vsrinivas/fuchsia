// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "garnet/bin/media/media_service/media_component_factory.h"
#include "garnet/bin/media/util/callback_joiner.h"
#include "garnet/bin/media/util/fidl_publisher.h"
#include "lib/fidl/cpp/binding.h"
#include <fuchsia/cpp/media.h>
#include <fuchsia/cpp/media.h>
#include <fuchsia/cpp/media.h>
#include <fuchsia/cpp/media.h>
#include <fuchsia/cpp/media.h>
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_function.h"

namespace media {

// Fidl agent that renders streams derived from a SeekingReader.
class MediaPlayerImpl
    : public MediaComponentFactory::MultiClientProduct<MediaPlayer>,
      public MediaPlayer {
 public:
  static std::shared_ptr<MediaPlayerImpl> Create(
      f1dl::InterfaceRequest<MediaPlayer> request,
      MediaComponentFactory* owner);

  ~MediaPlayerImpl() override;

  // MediaPlayer implementation.
  void SetHttpSource(const f1dl::StringPtr& http_url) override;

  void SetFileSource(zx::channel file_channel) override;

  void SetReaderSource(
      f1dl::InterfaceHandle<SeekingReader> reader_handle) override;

  void Play() override;

  void Pause() override;

  void Seek(int64_t position) override;

  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

  void SetGain(float gain) override;

  void CreateView(
      f1dl::InterfaceHandle<views_v1::ViewManager> view_manager,
      f1dl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request) override;

  void SetAudioRenderer(
      f1dl::InterfaceHandle<AudioRenderer> audio_renderer,
      f1dl::InterfaceHandle<MediaRenderer> media_renderer) override;

  void AddBinding(f1dl::InterfaceRequest<MediaPlayer> request) override;

 private:
  static constexpr int64_t kMinimumLeadTime = Timeline::ns_from_ms(30);

  // Internal state.
  enum class State {
    kInactive,  // Waiting for a reader to be supplied.
    kWaiting,   // Waiting for some work to complete.
    kFlushed,   // Paused with no data in the pipeline.
    kPrimed,    // Paused with data in the pipeline.
    kPlaying,   // Time is progressing.
  };

  // Holds per-stream info. |renderer_handle_| remains set until the renderer is
  // needed, at which point |renderer_handle_| is cleared and |sink_| is set.
  // Media for which no renderer was supplied are not represented in
  // |streams_by_medium_|.
  struct Stream {
    f1dl::InterfaceHandle<MediaRenderer> renderer_handle_;
    MediaSinkPtr sink_;
    bool connected_ = false;
  };

  MediaPlayerImpl(f1dl::InterfaceRequest<MediaPlayer> request,
                  MediaComponentFactory* owner);

  // Sets the video renderer.
  // TODO(dalesat): Remove after topaz transition.
  void SetVideoRenderer(f1dl::InterfaceHandle<MediaRenderer> video_renderer);

  // If |reader_handle_| is set, creates the source and calls |ConnectSinks|,
  // otherwise does nothing.
  void MaybeCreateSource();

  // Creates the renderer for |medium| if it doesn't exist already.
  void MaybeCreateRenderer(MediaTypeMedium medium);

  // Creates sinks as needed and connects enabled streams.
  void ConnectSinks();

  // Prepares a stream.
  void PrepareStream(Stream* stream,
                     size_t index,
                     const MediaTypePtr& input_media_type,
                     const std::function<void()>& callback);

  // Takes action based on current state.
  void Update();

  // Sets the timeline transform.
  void SetTimelineTransform(
      float rate,
      int64_t reference_time,
      const TimelineConsumer::SetTimelineTransformCallback callback);

  // Creates a TimelineTransform for the specified rate.
  TimelineTransformPtr CreateTimelineTransform(float rate,
                                               int64_t reference_time);

  // Handles a status update from the source. When called with the default
  // argument values, initiates source status updates.
  void HandleSourceStatusUpdates(uint64_t version = kInitialStatus,
                                 MediaSourceStatusPtr status = nullptr);

  // Handles a status update from the control point. When called with the
  // default argument values, initiates control point. status updates.
  void HandleTimelineControlPointStatusUpdates(
      uint64_t version = kInitialStatus,
      MediaTimelineControlPointStatus* status = nullptr);

  f1dl::InterfaceHandle<SeekingReader> reader_handle_;
  MediaSourcePtr source_;
  f1dl::VectorPtr<MediaTypePtr> stream_types_;
  std::unordered_map<MediaTypeMedium, Stream> streams_by_medium_;
  MediaTimelineControllerPtr timeline_controller_;
  MediaTimelineControlPointPtr timeline_control_point_;
  TimelineConsumerPtr timeline_consumer_;
  bool reader_transition_pending_ = false;
  float gain_ = 1.0f;
  f1dl::InterfacePtr<AudioRenderer> audio_renderer_;
  std::shared_ptr<VideoRendererImpl> video_renderer_impl_;

  // The state we're currently in.
  State state_ = State::kWaiting;

  // The state we're trying to transition to, either because the client has
  // called |Play| or |Pause| or because we've hit end-of-stream.
  State target_state_ = State::kFlushed;

  // Whether we're currently at end-of-stream.
  bool end_of_stream_ = false;

  // The position we want to seek to (because the client called Seek) or
  // kUnspecifiedTime, which indicates there's no desire to seek.
  int64_t target_position_ = kUnspecifiedTime;

  // The subject time to be used for SetTimelineTransform. The value is
  // kUnspecifiedTime if there's no need to seek or the position we want to
  // seek to if there is.
  int64_t transform_subject_time_ = kUnspecifiedTime;

  // The minimum program range PTS to be used for SetProgramRange.
  int64_t program_range_min_pts_ = kMinTime;

  // A function that translates local time into presentation time in ns.
  TimelineFunction timeline_function_;

  MediaSourceStatusPtr source_status_;
  FidlPublisher<GetStatusCallback> status_publisher_;
};

}  // namespace media
