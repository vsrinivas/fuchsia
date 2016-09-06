// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/media_service/media_player_impl.h"

#include "apps/media/cpp/timeline.h"
#include "apps/media/services/framework/parts/reader.h"
#include "apps/media/services/framework/util/callback_joiner.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/connect.h"

namespace mojo {
namespace media {

// static
std::shared_ptr<MediaPlayerImpl> MediaPlayerImpl::Create(
    InterfaceHandle<SeekingReader> reader,
    InterfaceHandle<MediaRenderer> audio_renderer,
    InterfaceHandle<MediaRenderer> video_renderer,
    InterfaceRequest<MediaPlayer> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<MediaPlayerImpl>(
      new MediaPlayerImpl(reader.Pass(), audio_renderer.Pass(),
                          video_renderer.Pass(), request.Pass(), owner));
}

MediaPlayerImpl::MediaPlayerImpl(InterfaceHandle<SeekingReader> reader,
                                 InterfaceHandle<MediaRenderer> audio_renderer,
                                 InterfaceHandle<MediaRenderer> video_renderer,
                                 InterfaceRequest<MediaPlayer> request,
                                 MediaServiceImpl* owner)
    : MediaServiceImpl::Product<MediaPlayer>(this, request.Pass(), owner) {
  FTL_DCHECK(reader);

  status_publisher_.SetCallbackRunner([this](const GetStatusCallback& callback,
                                             uint64_t version) {
    MediaPlayerStatusPtr status = MediaPlayerStatus::New();
    status->timeline_transform = TimelineTransform::From(timeline_function_);
    status->end_of_stream = end_of_stream_;
    status->metadata = metadata_.Clone();
    status->problem = demux_problem_.Clone();
    callback.Run(version, status.Pass());
  });

  state_ = State::kWaiting;

  ConnectToService(owner->shell(), "mojo:media_service", GetProxy(&factory_));

  factory_->CreateDemux(reader.Pass(), GetProxy(&demux_));
  HandleDemuxStatusUpdates();

  factory_->CreateTimelineController(GetProxy(&timeline_controller_));
  timeline_controller_->GetControlPoint(GetProxy(&timeline_control_point_));
  timeline_control_point_->GetTimelineConsumer(GetProxy(&timeline_consumer_));
  HandleTimelineControlPointStatusUpdates();

  audio_renderer_ = audio_renderer.Pass();
  video_renderer_ = video_renderer.Pass();

  demux_->Describe([this](Array<MediaTypePtr> stream_types) {
    FLOG(log_channel_, ReceivedDemuxDescription(stream_types.Clone()));
    // Populate streams_ and enable the streams we want.
    std::shared_ptr<CallbackJoiner> callback_joiner = CallbackJoiner::Create();

    for (MediaTypePtr& stream_type : stream_types) {
      streams_.push_back(std::unique_ptr<Stream>(new Stream()));
      Stream& stream = *streams_.back();
      switch (stream_type->medium) {
        case MediaTypeMedium::AUDIO:
          if (audio_renderer_) {
            stream.renderer_ = audio_renderer_.Pass();
            PrepareStream(&stream, streams_.size() - 1, stream_type,
                          callback_joiner->NewCallback());
          }
          break;
        case MediaTypeMedium::VIDEO:
          if (video_renderer_) {
            stream.renderer_ = video_renderer_.Pass();
            PrepareStream(&stream, streams_.size() - 1, stream_type,
                          callback_joiner->NewCallback());
          }
          break;
        // TODO(dalesat): Enable other stream types.
        default:
          break;
      }
    }

    callback_joiner->WhenJoined([this]() {
      FLOG(log_channel_, StreamsPrepared());
      // The enabled streams are prepared.
      factory_.reset();
      state_ = State::kFlushed;
      FLOG(log_channel_, Flushed());
      Update();
    });
  });
}

MediaPlayerImpl::~MediaPlayerImpl() {}

void MediaPlayerImpl::Update() {
  // This method is called whenever we might want to take action based on the
  // current state and recent events. The current state is in |state_|. Recent
  // events are recorded in |target_state_|, which indicates what state we'd
  // like to transition to, |target_position_|, which can indicate a position
  // we'd like to stream to, and |end_of_stream_| which tells us we've reached
  // end of stream.
  //
  // The states are as follows:
  //
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
      case State::kFlushed:
        // Presentation time is not progressing, and the pipeline is clear of
        // packets.
        if (target_position_ != kUnspecifiedTime) {
          // We want to seek. Enter |kWaiting| state until the operation is
          // complete.
          state_ = State::kWaiting;
          FLOG(log_channel_, Seeking(target_position_));
          demux_->Seek(target_position_, [this]() {
            transform_subject_time_ = target_position_;
            target_position_ = kUnspecifiedTime;
            state_ = State::kFlushed;
            FLOG(log_channel_, Flushed());
            // Back in |kFlushed|. Call |Update| to see if there's further
            // action to be taken.
            Update();
          });

          // Done for now. We're in kWaiting, and the callback will call Update
          // when the Seek call is complete.
          return;
        }

        if (target_state_ == State::kPlaying ||
            target_state_ == State::kPrimed) {
          // We want to transition to |kPrimed| or to |kPlaying|, for which
          // |kPrimed| is a prerequisite. We enter |kWaiting| state, issue the
          // |Prime| request and transition to |kPrimed| when the operation is
          // complete.
          state_ = State::kWaiting;
          FLOG(log_channel_, Priming());
          timeline_control_point_->Prime([this]() {
            state_ = State::kPrimed;
            FLOG(log_channel_, Primed());
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
            target_state_ == State::kFlushed) {
          // Either we want to seek or just want to transition to |kFlushed|.
          // We transition to |kWaiting|, issue the |Flush| request and
          // transition to |kFlushed| when the operation is complete.
          state_ = State::kWaiting;
          FLOG(log_channel_, Flushing());
          demux_->Flush([this]() {
            state_ = State::kFlushed;
            FLOG(log_channel_, Flushed());
            // Now we're in |kFlushed|. Call |Update| to see if there's further
            // action to be taken.
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
          TimelineTransformPtr timeline_transform =
              CreateTimelineTransform(1.0f);
          FLOG(log_channel_,
               SettingTimelineTransform(timeline_transform.Clone()));
          timeline_consumer_->SetTimelineTransform(
              timeline_transform.Pass(), [this](bool completed) {
                state_ = State::kPlaying;
                FLOG(log_channel_, Playing());
                // Now we're in |kPlaying|. Call |Update| to see if there's
                // further action to be taken.
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
            target_state_ == State::kPrimed) {
          // Either we want to seek or we want to stop playback. In either case,
          // we need to enter |kWaiting|, stop the presentation timeline and
          // transition to |kPrimed| when the operation completes.
          state_ = State::kWaiting;
          TimelineTransformPtr timeline_transform =
              CreateTimelineTransform(0.0f);
          FLOG(log_channel_,
               SettingTimelineTransform(timeline_transform.Clone()));
          timeline_consumer_->SetTimelineTransform(
              timeline_transform.Pass(), [this](bool completed) {
                state_ = State::kPrimed;
                FLOG(log_channel_, Primed());
                // Now we're in |kPrimed|. Call |Update| to see if there's
                // further action to be taken.
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
          FLOG(log_channel_, Primed());
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

TimelineTransformPtr MediaPlayerImpl::CreateTimelineTransform(float rate) {
  TimelineTransformPtr result = TimelineTransform::New();
  result->reference_time = Timeline::local_now() + kMinimumLeadTime;
  result->subject_time = transform_subject_time_;

  TimelineRate timeline_rate(rate);
  result->reference_delta = timeline_rate.reference_delta();
  result->subject_delta = timeline_rate.subject_delta();

  transform_subject_time_ = kUnspecifiedTime;

  return result.Pass();
}

void MediaPlayerImpl::GetStatus(uint64_t version_last_seen,
                                const GetStatusCallback& callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void MediaPlayerImpl::Play() {
  FLOG(log_channel_, PlayRequested());
  target_state_ = State::kPlaying;
  Update();
}

void MediaPlayerImpl::Pause() {
  FLOG(log_channel_, PauseRequested());
  target_state_ = State::kPrimed;
  Update();
}

void MediaPlayerImpl::Seek(int64_t position) {
  FLOG(log_channel_, SeekRequested(position));
  target_position_ = position;
  Update();
}

void MediaPlayerImpl::PrepareStream(Stream* stream,
                                    size_t index,
                                    const MediaTypePtr& input_media_type,
                                    const std::function<void()>& callback) {
  FTL_DCHECK(factory_);

  demux_->GetPacketProducer(index, GetProxy(&stream->encoded_producer_));

  if (input_media_type->encoding != MediaType::kAudioEncodingLpcm &&
      input_media_type->encoding != MediaType::kVideoEncodingUncompressed) {
    std::shared_ptr<CallbackJoiner> callback_joiner = CallbackJoiner::Create();

    // Compressed media. Insert a decoder in front of the sink. The sink would
    // add its own internal decoder, but we want to test the decoder.
    factory_->CreateDecoder(input_media_type.Clone(),
                            GetProxy(&stream->decoder_));

    MediaPacketConsumerPtr decoder_consumer;
    stream->decoder_->GetPacketConsumer(GetProxy(&decoder_consumer));

    callback_joiner->Spawn();
    stream->encoded_producer_->Connect(decoder_consumer.Pass(),
                                       [stream, callback_joiner]() {
                                         stream->encoded_producer_.reset();
                                         callback_joiner->Complete();
                                       });

    callback_joiner->Spawn();
    stream->decoder_->GetOutputType([this, stream, callback_joiner](
        MediaTypePtr output_type) {
      stream->decoder_->GetPacketProducer(GetProxy(&stream->decoded_producer_));
      CreateSink(stream, output_type, callback_joiner->NewCallback());
      callback_joiner->Complete();
    });

    callback_joiner->WhenJoined(callback);
  } else {
    // Uncompressed media. Connect the demux stream directly to the sink. This
    // would work for compressed media as well (the sink would decode), but we
    // want to test the decoder.
    stream->decoded_producer_ = stream->encoded_producer_.Pass();
    CreateSink(stream, input_media_type, callback);
  }
}

void MediaPlayerImpl::CreateSink(Stream* stream,
                                 const MediaTypePtr& input_media_type,
                                 const std::function<void()>& callback) {
  FTL_DCHECK(input_media_type);
  FTL_DCHECK(stream->decoded_producer_);
  FTL_DCHECK(factory_);

  factory_->CreateSink(stream->renderer_.Pass(), input_media_type.Clone(),
                       GetProxy(&stream->sink_));

  MediaTimelineControlPointPtr timeline_control_point;
  stream->sink_->GetTimelineControlPoint(GetProxy(&timeline_control_point));

  timeline_controller_->AddControlPoint(timeline_control_point.Pass());

  MediaPacketConsumerPtr consumer;
  stream->sink_->GetPacketConsumer(GetProxy(&consumer));

  stream->decoded_producer_->Connect(consumer.Pass(),
                                     [this, callback, stream]() {
                                       stream->decoded_producer_.reset();
                                       callback();
                                     });
}

void MediaPlayerImpl::HandleDemuxStatusUpdates(uint64_t version,
                                               MediaDemuxStatusPtr status) {
  if (status) {
    metadata_ = status->metadata.Pass();
    demux_problem_ = status->problem.Pass();
    status_publisher_.SendUpdates();
  }

  demux_->GetStatus(version,
                    [this](uint64_t version, MediaDemuxStatusPtr status) {
                      HandleDemuxStatusUpdates(version, status.Pass());
                    });
}

void MediaPlayerImpl::HandleTimelineControlPointStatusUpdates(
    uint64_t version,
    MediaTimelineControlPointStatusPtr status) {
  if (status) {
    timeline_function_ = status->timeline_transform.To<TimelineFunction>();
    end_of_stream_ = status->end_of_stream;
    status_publisher_.SendUpdates();
    Update();
  }

  timeline_control_point_->GetStatus(
      version,
      [this](uint64_t version, MediaTimelineControlPointStatusPtr status) {
        HandleTimelineControlPointStatusUpdates(version, status.Pass());
      });
}

MediaPlayerImpl::Stream::Stream() {}

MediaPlayerImpl::Stream::~Stream() {}

}  // namespace media
}  // namespace mojo
