// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_MEDIA_SERVICE_MEDIA_PLAYER_IMPL_H_
#define APPS_MEDIA_SERVICES_MEDIA_SERVICE_MEDIA_PLAYER_IMPL_H_

#include <limits>
#include <vector>

#include "apps/media/cpp/flog.h"
#include "apps/media/cpp/timeline.h"
#include "apps/media/cpp/timeline_function.h"
#include "apps/media/interfaces/logs/media_player_channel.mojom.h"
#include "apps/media/interfaces/media_service.mojom.h"
#include "apps/media/interfaces/media_transport.mojom.h"
#include "apps/media/interfaces/seeking_reader.mojom.h"
#include "apps/media/interfaces/timeline_controller.mojom.h"
#include "apps/media/services/common/mojo_publisher.h"
#include "apps/media/services/framework/util/callback_joiner.h"
#include "apps/media/services/media_service/media_service_impl.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace mojo {
namespace media {

// Mojo agent that renders streams from an origin specified by URL.
class MediaPlayerImpl : public MediaServiceImpl::Product<MediaPlayer>,
                        public MediaPlayer {
 public:
  static std::shared_ptr<MediaPlayerImpl> Create(
      InterfaceHandle<SeekingReader> reader,
      InterfaceHandle<MediaRenderer> audio_renderer,
      InterfaceHandle<MediaRenderer> video_renderer,
      InterfaceRequest<MediaPlayer> request,
      MediaServiceImpl* owner);

  ~MediaPlayerImpl() override;

  // MediaPlayer implementation.
  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

  void Play() override;

  void Pause() override;

  void Seek(int64_t position) override;

 private:
  static constexpr int64_t kMinimumLeadTime = Timeline::ns_from_ms(30);

  // Internal state.
  enum class State {
    kWaiting,  // Waiting for some work to complete.
    kFlushed,  // Paused with no data in the pipeline.
    kPrimed,   // Paused with data in the pipeline.
    kPlaying,  // Time is progressing.
  };

  struct Stream {
    Stream();
    ~Stream();
    // TODO(dalesat): Have the sink enlist the decoder.
    MediaTypeConverterPtr decoder_;
    MediaSinkPtr sink_;
    // The following fields are just temporaries used to solve lambda capture
    // problems.
    MediaPacketProducerPtr encoded_producer_;
    MediaPacketProducerPtr decoded_producer_;
    InterfaceHandle<MediaRenderer> renderer_;
  };

  MediaPlayerImpl(InterfaceHandle<SeekingReader> reader,
                  InterfaceHandle<MediaRenderer> audio_renderer,
                  InterfaceHandle<MediaRenderer> video_renderer,
                  InterfaceRequest<MediaPlayer> request,
                  MediaServiceImpl* owner);

  // Takes action based on current state.
  void Update();

  // Creates a TimelineTransform for the specified rate.
  TimelineTransformPtr CreateTimelineTransform(float rate);

  // Prepares a stream.
  void PrepareStream(Stream* stream,
                     size_t index,
                     const MediaTypePtr& input_media_type,
                     const std::function<void()>& callback);

  // Creates a sink for a stream.
  void CreateSink(Stream* stream,
                  const MediaTypePtr& input_media_type,
                  const std::function<void()>& callback);

  // Handles a status update from the demux. When called with the default
  // argument values, initiates demux status updates.
  void HandleDemuxStatusUpdates(uint64_t version = MediaDemux::kInitialMetadata,
                                MediaDemuxStatusPtr status = nullptr);

  // Handles a status update from the control point. When called with the
  // default argument values, initiates control point. status updates.
  void HandleTimelineControlPointStatusUpdates(
      uint64_t version = MediaTimelineControlPoint::kInitialStatus,
      MediaTimelineControlPointStatusPtr status = nullptr);

  MediaServicePtr factory_;
  MediaDemuxPtr demux_;
  MediaTimelineControllerPtr timeline_controller_;
  MediaTimelineControlPointPtr timeline_control_point_;
  TimelineConsumerPtr timeline_consumer_;
  std::vector<std::unique_ptr<Stream>> streams_;

  // The state we're currently in.
  State state_ = State::kWaiting;

  // The state we trying to transition to, either because the client has called
  // |Play| or |Pause| or because we've hit end-of-stream.
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

  // A function that translates local time into presentation time in ns.
  TimelineFunction timeline_function_;

  CallbackJoiner set_transform_joiner_;
  MediaMetadataPtr metadata_;
  ProblemPtr demux_problem_;
  MojoPublisher<GetStatusCallback> status_publisher_;

  // The following fields are just temporaries used to solve lambda capture
  // problems.
  InterfaceHandle<MediaRenderer> audio_renderer_;
  InterfaceHandle<MediaRenderer> video_renderer_;

  FLOG_INSTANCE_CHANNEL(logs::MediaPlayerChannel, log_channel_);
};

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_MEDIA_SERVICE_MEDIA_PLAYER_IMPL_H_
