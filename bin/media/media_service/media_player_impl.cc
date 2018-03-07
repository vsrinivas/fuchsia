// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_service/media_player_impl.h"

#include "garnet/bin/media/fidl/fidl_formatting.h"
#include "garnet/bin/media/media_service/video_renderer_impl.h"
#include "garnet/bin/media/util/callback_joiner.h"
#include "lib/app/cpp/connect.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/media/fidl/audio_server.fidl.h"
#include "lib/media/timeline/timeline.h"

namespace media {

// static
std::shared_ptr<MediaPlayerImpl> MediaPlayerImpl::Create(
    f1dl::InterfaceRequest<MediaPlayer> request,
    MediaComponentFactory* owner) {
  return std::shared_ptr<MediaPlayerImpl>(
      new MediaPlayerImpl(std::move(request), owner));
}

MediaPlayerImpl::MediaPlayerImpl(f1dl::InterfaceRequest<MediaPlayer> request,
                                 MediaComponentFactory* owner)
    : MediaComponentFactory::Product<MediaPlayer>(this,
                                                  std::move(request),
                                                  owner) {
  FXL_DCHECK(owner);

  status_publisher_.SetCallbackRunner(
      [this](const GetStatusCallback& callback, uint64_t version) {
        MediaPlayerStatusPtr status = MediaPlayerStatus::New();
        status->timeline_transform =
            static_cast<TimelineTransformPtr>(timeline_function_);
        status->end_of_stream = end_of_stream_;

        if (stream_types_) {
          for (MediaTypePtr& stream_type : stream_types_) {
            switch (stream_type->medium) {
              case MediaTypeMedium::AUDIO:
                status->content_has_audio = true;
                break;
              case MediaTypeMedium::VIDEO:
                status->content_has_video = true;
                break;
              default:
                break;
            }
          }
        }

        if (source_status_) {
          status->audio_connected = source_status_->audio_connected;
          status->video_connected = source_status_->video_connected;
          status->metadata = source_status_->metadata.Clone();

          if (video_renderer_impl_) {
            status->video_size = video_renderer_impl_->GetSize().Clone();
            status->pixel_aspect_ratio =
                video_renderer_impl_->GetPixelAspectRatio().Clone();
          }

          if (source_status_->problem) {
            status->problem = source_status_->problem.Clone();
          } else if (state_ >= State::kFlushed && !status->audio_connected &&
                     !status->video_connected) {
            // The source isn't reporting a problem, but neither audio nor video
            // is connected. We report this as a problem so the client doesn't
            // have to check these values separately.
            status->problem = Problem::New();
            status->problem->type = Problem::kProblemMediaTypeNotSupported;
          }
        }

        callback(version, std::move(status));
      });

  state_ = State::kInactive;

  // Create a timeline controller.
  owner->CreateTimelineController(timeline_controller_.NewRequest());
  timeline_controller_->GetControlPoint(timeline_control_point_.NewRequest());
  timeline_control_point_->GetTimelineConsumer(timeline_consumer_.NewRequest());
  HandleTimelineControlPointStatusUpdates();

  MaybeCreateSource();
}

MediaPlayerImpl::~MediaPlayerImpl() {
  if (video_renderer_impl_) {
    video_renderer_impl_->SetGeometryUpdateCallback(nullptr);
  }
}

void MediaPlayerImpl::MaybeCreateSource() {
  if (!reader_handle_) {
    return;
  }

  state_ = State::kWaiting;

  owner()->CreateSource(std::move(reader_handle_), nullptr,
                        source_.NewRequest());
  HandleSourceStatusUpdates();

  source_->Describe(
      fxl::MakeCopyable([this](f1dl::Array<MediaTypePtr> stream_types) mutable {
        stream_types_ = std::move(stream_types);
        ConnectSinks();
      }));
}

void MediaPlayerImpl::MaybeCreateRenderer(MediaTypeMedium medium) {
  if (streams_by_medium_.find(medium) != streams_by_medium_.end()) {
    // Renderer already exists.
    return;
  }

  auto& stream = streams_by_medium_[medium];

  switch (medium) {
    case MediaTypeMedium::AUDIO: {
      f1dl::InterfaceHandle<MediaRenderer> audio_media_renderer;
      auto audio_server = owner()->ConnectToEnvironmentService<AudioServer>();
      audio_server->CreateRenderer(audio_renderer_.NewRequest(),
                                   audio_media_renderer.NewRequest());
      stream.renderer_handle_ = std::move(audio_media_renderer);
      if (gain_ != 1.0f) {
        audio_renderer_->SetGain(gain_);
      }
    } break;
    case MediaTypeMedium::VIDEO: {
      f1dl::InterfaceHandle<MediaRenderer> video_media_renderer;
      video_renderer_impl_ =
          owner()->CreateVideoRenderer(video_media_renderer.NewRequest());
      stream.renderer_handle_ = std::move(video_media_renderer);
      video_renderer_impl_->SetGeometryUpdateCallback(
          [this]() { status_publisher_.SendUpdates(); });
    } break;
    default:
      FXL_DCHECK(false) << "Only audio and video are currently supported";
      break;
  }
}

void MediaPlayerImpl::ConnectSinks() {
  std::shared_ptr<CallbackJoiner> callback_joiner = CallbackJoiner::Create();

  size_t stream_index = 0;

  for (MediaTypePtr& stream_type : stream_types_) {
    MaybeCreateRenderer(stream_type->medium);

    auto iter = streams_by_medium_.find(stream_type->medium);
    if (iter != streams_by_medium_.end()) {
      auto& stream = iter->second;

      if (stream.connected_) {
        // TODO(dalesat): How do we choose the right stream?
        FXL_DLOG(INFO) << "Stream " << stream_index
                       << " redundant, already connected to sink with medium "
                       << stream_type->medium;
        ++stream_index;
        continue;
      }

      PrepareStream(&stream, stream_index, stream_type,
                    callback_joiner->NewCallback());
    }

    ++stream_index;
  }

  callback_joiner->WhenJoined([this]() {
    state_ = State::kFlushed;
    Update();
  });
}

void MediaPlayerImpl::PrepareStream(Stream* stream,
                                    size_t index,
                                    const MediaTypePtr& input_media_type,
                                    const std::function<void()>& callback) {
  if (!stream->sink_) {
    FXL_DCHECK(stream->renderer_handle_);
    owner()->CreateSink(std::move(stream->renderer_handle_),
                        stream->sink_.NewRequest());

    MediaTimelineControlPointPtr timeline_control_point;
    stream->sink_->GetTimelineControlPoint(timeline_control_point.NewRequest());

    timeline_controller_->AddControlPoint(std::move(timeline_control_point));
  }

  stream->sink_->ConsumeMediaType(
      input_media_type.Clone(),
      [this, stream, index,
       callback](f1dl::InterfaceHandle<MediaPacketConsumer> consumer) {
        if (!consumer) {
          // The sink couldn't build a conversion pipeline for the media type.
          callback();
          return;
        }

        stream->connected_ = true;

        MediaPacketProducerPtr producer;
        source_->GetPacketProducer(index, producer.NewRequest());

        // Capture producer so it survives through the callback.
        producer->Connect(consumer.Bind(), fxl::MakeCopyable([
                            this, callback, producer = std::move(producer)
                          ]() { callback(); }));
      });
}

void MediaPlayerImpl::Update() {
  // This method is called whenever we might want to take action based on the
  // current state and recent events. The current state is in |state_|. Recent
  // events are recorded in |target_state_|, which indicates what state we'd
  // like to transition to, |target_position_|, which can indicate a position
  // we'd like to stream to, and |end_of_stream_| which tells us we've reached
  // end of stream.
  //
  // Also relevant is |reader_transition_pending_|, which, when true, is treated
  // pretty much like a |target_state_| of kFlushed. It indicates that we have
  // a new reader we want to use, so the graph needs to be flushed and rebuilt.
  // We use it instead of |target_state_| so that |target_state_| is preserved
  // for when the new graph is built, at which point we'll work to transition
  // to |target_state_|.
  //
  // The states are as follows:
  //
  // |kInactive|- Indicates that we have no reader.
  // |kWaiting| - Indicates that we've done something asynchronous, and no
  //              further action should be taken by the state machine until that
  //              something completes (at which point the callback will change
  //              the state and call |Update|).
  // |kFlushed| - Indicates that presentation time is not progressing and that
  //              the pipeline is not primed with packets. This is the initial
  //              state and the state we transition to in preparation for
  //              seeking. A seek is currently only done when when the pipeline
  //              is clear of packets.
  // |kPrimed| -  Indicates that presentation time is not progressing and that
  //              the pipeline is primed with packets. We transition to this
  //              state when the client calls |Pause|, either from |kFlushed| or
  //              |kPlaying| state.
  // |kPlaying| - Indicates that presentation time is progressing and there are
  //              packets in the pipeline. We transition to this state when the
  //              client calls |Play|. If we're in |kFlushed| when |Play| is
  //              called, we transition through |kPrimed| state.
  //
  // The while loop that surrounds all the logic below is there because, after
  // taking some action and transitioning to a new state, we may want to check
  // to see if there's more to do in the new state. You'll also notice that
  // the callback lambdas generally call |Update|.
  while (true) {
    switch (state_) {
      case State::kInactive:
        if (!reader_transition_pending_) {
          return;
        }
      // Falls through.

      case State::kFlushed:
        // Presentation time is not progressing, and the pipeline is clear of
        // packets.
        if (reader_transition_pending_) {
          // We need to switch to a new reader. Destroy the current source.
          reader_transition_pending_ = false;
          state_ = State::kInactive;
          source_.Unbind();
          stream_types_.reset();
          source_status_.reset();
          for (auto& pair : streams_by_medium_) {
            pair.second.connected_ = false;
          }

          // The new source will start at position 0 unless a seek is requested.
          // We set |program_range_min_pts_| and |transform_subject_time_| so
          // the program range and timeline will be set properly.
          // TODO(dalesat): Should |program_range_min_pts_| be kMinTime?
          program_range_min_pts_ = 0;
          transform_subject_time_ = 0;
          status_publisher_.SendUpdates();
          MaybeCreateSource();
          return;
        }

        if (target_position_ != kUnspecifiedTime) {
          // We want to seek. Enter |kWaiting| state until the operation is
          // complete.
          state_ = State::kWaiting;

          // Capture the target position and clear it. If we get another seek
          // request while setting the timeline transform and and seeking the
          // source, we'll notice that and do those things again.
          int64_t target_position = target_position_;
          target_position_ = kUnspecifiedTime;

          // |program_range_min_pts_| will be delivered in the |SetProgramRange|
          // call, ensuring that the renderers discard packets with PTS values
          // less than the target position. |transform_subject_time_| is used
          // when setting the timeline.
          transform_subject_time_ = target_position;
          program_range_min_pts_ = target_position;

          SetTimelineTransform(
              0.0f, Timeline::local_now(),
              [this, target_position](bool completed) {
                if (target_position_ == target_position) {
                  // We've had a rendundant seek request. Ignore it.
                  target_position_ = kUnspecifiedTime;
                } else if (target_position_ != kUnspecifiedTime) {
                  // We've had a seek request to a new position. Refrain from
                  // seeking the source and re-enter this sequence.
                  state_ = State::kFlushed;
                  Update();
                  return;
                }

                // Seek to the new position.
                source_->Seek(target_position, [this]() {
                  state_ = State::kFlushed;
                  // Back in |kFlushed|. Call |Update| to see
                  // if there's further action to be taken.
                  Update();
                });
              });

          // Done for now. We're in kWaiting, and the callback will call Update
          // when the Seek call is complete.
          return;
        }

        if (target_state_ == State::kPlaying ||
            target_state_ == State::kPrimed) {
          // We want to transition to |kPrimed| or to |kPlaying|, for which
          // |kPrimed| is a prerequisite. We enter |kWaiting| state, issue the
          // |SetProgramRange| and |Prime| requests and transition to |kPrimed|
          // when the operation is complete.
          state_ = State::kWaiting;
          timeline_control_point_->SetProgramRange(0, program_range_min_pts_,
                                                   kMaxTime);

          timeline_control_point_->Prime([this]() {
            state_ = State::kPrimed;
            // Now we're in |kPrimed|. Call |Update| to see if there's further
            // action to be taken.
            Update();
          });

          // Done for now. We're in |kWaiting|, and the callback will call
          // |Update| when the prime is complete.
          return;
        }

        // No interesting events to respond to. Done for now.
        return;

      case State::kPrimed:
        // Presentation time is not progressing, and the pipeline is primed with
        // packets.
        if (target_position_ != kUnspecifiedTime ||
            target_state_ == State::kFlushed || reader_transition_pending_) {
          // Either we want to seek or just want to transition to |kFlushed|,
          // possibly because a reader transition is pending. We transition to
          // |kWaiting|, issue the |Flush| request and transition to |kFlushed|
          // when the operation is complete.
          state_ = State::kWaiting;
          source_->Flush(
              target_state_ != State::kFlushed && !reader_transition_pending_,
              [this]() {
                state_ = State::kFlushed;
                // Now we're in |kFlushed|. Call |Update| to see if there's
                // further action to be taken.
                Update();
              });

          // Done for now. We're in |kWaiting|, and the callback will call
          // |Update| when the flush is complete.
          return;
        }

        if (target_state_ == State::kPlaying) {
          // We want to transition to |kPlaying|. Enter |kWaiting|, start the
          // presentation timeline and transition to |kPlaying| when the
          // operation completes.
          state_ = State::kWaiting;
          SetTimelineTransform(1.0f, Timeline::local_now() + kMinimumLeadTime,
                               [this](bool completed) {
                                 state_ = State::kPlaying;
                                 // Now we're in |kPlaying|. Call |Update| to
                                 // see if there's further action to be taken.
                                 Update();
                               });

          // Done for now. We're in |kWaiting|, and the callback will call
          // |Update| when the flush is complete.
          return;
        }

        // No interesting events to respond to. Done for now.
        return;

      case State::kPlaying:
        // Presentation time is progressing, and packets are moving through
        // the pipeline.
        if (target_position_ != kUnspecifiedTime ||
            target_state_ == State::kFlushed ||
            target_state_ == State::kPrimed || reader_transition_pending_) {
          // Either we want to seek or we want to stop playback, possibly
          // because a reader transition is pending. In either case, we need
          // to enter |kWaiting|, stop the presentation timeline and transition
          // to |kPrimed| when the operation completes.
          state_ = State::kWaiting;
          SetTimelineTransform(0.0f, Timeline::local_now() + kMinimumLeadTime,
                               [this](bool completed) {
                                 state_ = State::kPrimed;
                                 // Now we're in |kPrimed|. Call |Update| to see
                                 // if there's further action to be taken.
                                 Update();
                               });

          // Done for now. We're in |kWaiting|, and the callback will call
          // |Update| when the flush is complete.
          return;
        }

        if (end_of_stream_) {
          // We've reached end of stream. The presentation timeline stops by
          // itself, so we just need to transition to |kPrimed|.
          target_state_ = State::kPrimed;
          state_ = State::kPrimed;
          // Loop around to check if there's more work to do.
          break;
        }

        // No interesting events to respond to. Done for now.
        return;

      case State::kWaiting:
        // Waiting for some async operation. Nothing to do until it completes.
        return;
    }
  }
}

void MediaPlayerImpl::SetTimelineTransform(
    float rate,
    int64_t reference_time,
    const TimelineConsumer::SetTimelineTransformCallback callback) {
  TimelineTransformPtr timeline_transform =
      CreateTimelineTransform(rate, reference_time);
  timeline_consumer_->SetTimelineTransform(std::move(timeline_transform),
                                           callback);
}

TimelineTransformPtr MediaPlayerImpl::CreateTimelineTransform(
    float rate,
    int64_t reference_time) {
  TimelineTransformPtr result = TimelineTransform::New();
  result->reference_time = reference_time;
  result->subject_time = transform_subject_time_;

  TimelineRate timeline_rate(rate);
  result->reference_delta = timeline_rate.reference_delta();
  result->subject_delta = timeline_rate.subject_delta();

  transform_subject_time_ = kUnspecifiedTime;

  return result;
}

void MediaPlayerImpl::SetHttpSource(const f1dl::String& http_url) {
  f1dl::InterfaceHandle<SeekingReader> reader;
  owner()->CreateHttpReader(http_url, reader.NewRequest());
  SetReader(std::move(reader));
}

void MediaPlayerImpl::SetFileSource(zx::channel file_channel) {
  f1dl::InterfaceHandle<SeekingReader> reader;
  owner()->CreateFileChannelReader(std::move(file_channel),
                                   reader.NewRequest());
  SetReader(std::move(reader));
}

void MediaPlayerImpl::SetReaderSource(
    f1dl::InterfaceHandle<SeekingReader> reader_handle) {
  if (!reader_handle && !source_) {
    // There was already no reader. Nothing to do.
    return;
  }

  // Setting reader_transition_pending_ has a similar effect to setting
  // target_state_ to State::kFlushed. We don't change target_state_ so the
  // player will respect the client's desires once the reader transition is
  // complete.
  reader_transition_pending_ = true;
  reader_handle_ = std::move(reader_handle);

  // We clear |target_position_| so that a previously-requested seek that's
  // still pending will not be applied to the new reader. The client can
  // call |Seek| between this point and when the new graph is set up, and it
  // will work.
  target_position_ = kUnspecifiedTime;

  Update();
}

void MediaPlayerImpl::Play() {
  target_state_ = State::kPlaying;
  Update();
}

void MediaPlayerImpl::Pause() {
  target_state_ = State::kPrimed;
  Update();
}

void MediaPlayerImpl::Seek(int64_t position) {
  target_position_ = position;
  Update();
}

void MediaPlayerImpl::GetStatus(uint64_t version_last_seen,
                                const GetStatusCallback& callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void MediaPlayerImpl::SetGain(float gain) {
  if (audio_renderer_) {
    audio_renderer_->SetGain(gain);
  } else {
    gain_ = gain;
  }
}

void MediaPlayerImpl::CreateView(
    f1dl::InterfaceHandle<mozart::ViewManager> view_manager,
    f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  MaybeCreateRenderer(MediaTypeMedium::VIDEO);
  if (!video_renderer_impl_) {
    return;
  }

  video_renderer_impl_->CreateView(view_manager.Bind(),
                                   std::move(view_owner_request));
}

void MediaPlayerImpl::SetAudioRenderer(
    f1dl::InterfaceHandle<AudioRenderer> audio_renderer,
    f1dl::InterfaceHandle<MediaRenderer> media_renderer) {
  if (streams_by_medium_.find(MediaTypeMedium::AUDIO) !=
      streams_by_medium_.end()) {
    // We already have this renderer. Do nothing.
    return;
  }

  streams_by_medium_[MediaTypeMedium::AUDIO].renderer_handle_ =
      std::move(media_renderer);

  if (audio_renderer) {
    audio_renderer_.Bind(std::move(audio_renderer));
  }
}

void MediaPlayerImpl::SetReader(
    f1dl::InterfaceHandle<SeekingReader> reader_handle) {
  SetReaderSource(std::move(reader_handle));
}

void MediaPlayerImpl::SetVideoRenderer(
    f1dl::InterfaceHandle<MediaRenderer> media_renderer) {
  if (streams_by_medium_.find(MediaTypeMedium::VIDEO) !=
      streams_by_medium_.end()) {
    // We already have this renderer. Do nothing.
    return;
  }

  streams_by_medium_[MediaTypeMedium::VIDEO].renderer_handle_ =
      std::move(media_renderer);
}

void MediaPlayerImpl::HandleSourceStatusUpdates(uint64_t version,
                                                MediaSourceStatusPtr status) {
  if (status) {
    source_status_ = std::move(status);
    status_publisher_.SendUpdates();
  }

  source_->GetStatus(version,
                     [this](uint64_t version, MediaSourceStatusPtr status) {
                       HandleSourceStatusUpdates(version, std::move(status));
                     });
}

void MediaPlayerImpl::HandleTimelineControlPointStatusUpdates(
    uint64_t version,
    MediaTimelineControlPointStatusPtr status) {
  if (status) {
    timeline_function_ =
        static_cast<TimelineFunction>(status->timeline_transform);
    end_of_stream_ = status->end_of_stream;
    status_publisher_.SendUpdates();
    Update();
  }

  timeline_control_point_->GetStatus(
      version,
      [this](uint64_t version, MediaTimelineControlPointStatusPtr status) {
        HandleTimelineControlPointStatusUpdates(version, std::move(status));
      });
}

}  // namespace media
