// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/media_player_impl.h"

#include <fuchsia/cpp/media.h>
#include <fuchsia/cpp/media_player.h>
#include <lib/async/default.h>

#include "garnet/bin/media/media_player/demux/fidl_reader.h"
#include "garnet/bin/media/media_player/demux/file_reader.h"
#include "garnet/bin/media/media_player/demux/http_reader.h"
#include "garnet/bin/media/media_player/demux/reader_cache.h"
#include "garnet/bin/media/media_player/fidl/fidl_formatting.h"
#include "garnet/bin/media/media_player/fidl/fidl_type_conversions.h"
#include "garnet/bin/media/media_player/player/demux_source_segment.h"
#include "garnet/bin/media/media_player/player/renderer_sink_segment.h"
#include "garnet/bin/media/media_player/render/fidl_audio_renderer.h"
#include "garnet/bin/media/media_player/render/fidl_video_renderer.h"
#include "garnet/bin/media/media_player/util/safe_clone.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"

namespace media_player {

// static
std::unique_ptr<MediaPlayerImpl> MediaPlayerImpl::Create(
    fidl::InterfaceRequest<MediaPlayer> request,
    component::ApplicationContext* application_context,
    fxl::Closure quit_callback) {
  return std::make_unique<MediaPlayerImpl>(std::move(request),
                                           application_context, quit_callback);
}

MediaPlayerImpl::MediaPlayerImpl(
    fidl::InterfaceRequest<MediaPlayer> request,
    component::ApplicationContext* application_context,
    fxl::Closure quit_callback)
    : application_context_(application_context),
      quit_callback_(quit_callback),
      player_(async_get_default()) {
  FXL_DCHECK(request);
  FXL_DCHECK(application_context_);
  FXL_DCHECK(quit_callback_);

  AddBinding(std::move(request));

  bindings_.set_empty_set_handler([this]() { quit_callback_(); });

  player_.SetUpdateCallback([this]() {
    status_publisher_.SendUpdates();
    Update();
  });

  MaybeCreateRenderer(StreamType::Medium::kAudio);

  status_publisher_.SetCallbackRunner([this](GetStatusCallback callback,
                                             uint64_t version) {
    MediaPlayerStatus status;
    status.timeline_transform =
        fidl::MakeOptional(player_.timeline_function().ToTimelineTransform());
    status.end_of_stream = player_.end_of_stream();
    status.content_has_audio =
        player_.content_has_medium(StreamType::Medium::kAudio);
    status.content_has_video =
        player_.content_has_medium(StreamType::Medium::kVideo);
    status.audio_connected =
        player_.medium_connected(StreamType::Medium::kAudio);
    status.video_connected =
        player_.medium_connected(StreamType::Medium::kVideo);

    status.metadata = fxl::To<MediaMetadataPtr>(player_.metadata());

    if (video_renderer_) {
      status.video_size = SafeClone(video_renderer_->video_size());
      status.pixel_aspect_ratio =
          SafeClone(video_renderer_->pixel_aspect_ratio());
    }

    status.problem = SafeClone(player_.problem());

    callback(version, std::move(status));
  });

  state_ = State::kInactive;
}

MediaPlayerImpl::~MediaPlayerImpl() {
  player_.SetUpdateCallback(nullptr);

  if (video_renderer_) {
    video_renderer_->SetGeometryUpdateCallback(nullptr);
  }
}

void MediaPlayerImpl::MaybeCreateRenderer(StreamType::Medium medium) {
  if (player_.has_sink_segment(medium)) {
    // Renderer already exists.
    return;
  }

  switch (medium) {
    case StreamType::Medium::kAudio:
      if (!audio_renderer_) {
        auto audio_server =
            application_context_
                ->ConnectToEnvironmentService<media::AudioServer>();
        media::AudioRenderer2Ptr audio_renderer;
        audio_server->CreateRendererV2(audio_renderer.NewRequest());
        audio_renderer_ = FidlAudioRenderer::Create(std::move(audio_renderer));
        if (gain_ != 1.0f) {
          audio_renderer_->SetGain(gain_);
        }

        player_.SetSinkSegment(RendererSinkSegment::Create(audio_renderer_),
                               medium);
      }
      break;
    case StreamType::Medium::kVideo:
      if (!video_renderer_) {
        video_renderer_ = FidlVideoRenderer::Create();
        video_renderer_->SetGeometryUpdateCallback(
            [this]() { status_publisher_.SendUpdates(); });

        player_.SetSinkSegment(RendererSinkSegment::Create(video_renderer_),
                               medium);
      }
      break;
    default:
      FXL_DCHECK(false) << "Only audio and video are currently supported";
      break;
  }
}

void MediaPlayerImpl::Update() {
  // This method is called whenever we might want to take action based on the
  // current state and recent events. The current state is in |state_|. Recent
  // events are recorded in |target_state_|, which indicates what state we'd
  // like to transition to, |target_position_|, which can indicate a position
  // we'd like to stream to, and |player_.end_of_stream()| which tells us we've
  // reached end of stream.
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
        return;

      case State::kFlushed:
        // Presentation time is not progressing, and the pipeline is clear of
        // packets.
        if (target_position_ != media::kUnspecifiedTime) {
          // We want to seek. Enter |kWaiting| state until the operation is
          // complete.
          state_ = State::kWaiting;

          // Capture the target position and clear it. If we get another seek
          // request while setting the timeline transform and and seeking the
          // source, we'll notice that and do those things again.
          int64_t target_position = target_position_;
          target_position_ = media::kUnspecifiedTime;

          // |program_range_min_pts_| will be delivered in the |SetProgramRange|
          // call, ensuring that the renderers discard packets with PTS values
          // less than the target position. |transform_subject_time_| is used
          // when setting the timeline.
          transform_subject_time_ = target_position;
          program_range_min_pts_ = target_position;

          SetTimelineFunction(
              0.0f, media::Timeline::local_now(), [this, target_position]() {
                if (target_position_ == target_position) {
                  // We've had a rendundant seek request. Ignore it.
                  target_position_ = media::kUnspecifiedTime;
                } else if (target_position_ != media::kUnspecifiedTime) {
                  // We've had a seek request to a new position. Refrain from
                  // seeking the source and re-enter this sequence.
                  state_ = State::kFlushed;
                  Update();
                  return;
                }

                // Seek to the new position.
                player_.Seek(target_position, [this]() {
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
          player_.SetProgramRange(0, program_range_min_pts_, media::kMaxTime);

          player_.Prime([this]() {
            state_ = State::kPrimed;
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
        if (target_position_ != media::kUnspecifiedTime ||
            target_state_ == State::kFlushed) {
          // Either we want to seek, or we otherwise want to flush.
          player_.Flush(target_state_ != State::kFlushed);
          state_ = State::kFlushed;
          break;
        }

        if (target_state_ == State::kPlaying) {
          // We want to transition to |kPlaying|. Enter |kWaiting|, start the
          // presentation timeline and transition to |kPlaying| when the
          // operation completes.
          state_ = State::kWaiting;
          SetTimelineFunction(
              1.0f, media::Timeline::local_now() + kMinimumLeadTime, [this]() {
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
        if (target_position_ != media::kUnspecifiedTime ||
            target_state_ == State::kFlushed ||
            target_state_ == State::kPrimed) {
          // Either we want to seek or we want to stop playback, possibly
          // because a reader transition is pending. In either case, we need
          // to enter |kWaiting|, stop the presentation timeline and transition
          // to |kPrimed| when the operation completes.
          state_ = State::kWaiting;
          SetTimelineFunction(
              0.0f, media::Timeline::local_now() + kMinimumLeadTime, [this]() {
                state_ = State::kPrimed;
                // Now we're in |kPrimed|. Call |Update| to see
                // if there's further action to be taken.
                Update();
              });

          // Done for now. We're in |kWaiting|, and the callback will call
          // |Update| when the flush is complete.
          return;
        }

        if (player_.end_of_stream()) {
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

void MediaPlayerImpl::SetTimelineFunction(float rate,
                                          int64_t reference_time,
                                          fxl::Closure callback) {
  player_.SetTimelineFunction(
      media::TimelineFunction(transform_subject_time_, reference_time,
                              media::TimelineRate(rate)),
      callback);
  transform_subject_time_ = media::kUnspecifiedTime;
  status_publisher_.SendUpdates();
}

void MediaPlayerImpl::SetHttpSource(fidl::StringPtr http_url) {
  SetReader(HttpReader::Create(application_context_, http_url));
}

void MediaPlayerImpl::SetFileSource(zx::channel file_channel) {
  SetReader(FileReader::Create(std::move(file_channel)));
}

void MediaPlayerImpl::SetReaderSource(
    fidl::InterfaceHandle<SeekingReader> reader_handle) {
  if (!reader_handle) {
    player_.SetSourceSegment(nullptr, nullptr);
    return;
  }

  SetReader(FidlReader::Create(reader_handle.Bind()));
}

void MediaPlayerImpl::SetReader(std::shared_ptr<Reader> reader) {
  state_ = State::kWaiting;
  target_position_ = media::kUnspecifiedTime;
  program_range_min_pts_ = 0;
  transform_subject_time_ = 0;

  std::shared_ptr<Demux> demux = Demux::Create(ReaderCache::Create(reader));
  FXL_DCHECK(demux);

  player_.SetSourceSegment(DemuxSourceSegment::Create(demux), [this]() {
    state_ = State::kFlushed;
    status_publisher_.SendUpdates();
    Update();
  });
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
                                GetStatusCallback callback) {
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
    fidl::InterfaceHandle<views_v1::ViewManager> view_manager,
    fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request) {
  MaybeCreateRenderer(StreamType::Medium::kVideo);
  if (!video_renderer_) {
    return;
  }

  video_renderer_->CreateView(view_manager.Bind(),
                              std::move(view_owner_request));
}

void MediaPlayerImpl::SetAudioRenderer(
    fidl::InterfaceHandle<media::AudioRenderer> audio_renderer,
    fidl::InterfaceHandle<media::MediaRenderer> media_renderer) {
  // We're using AudioRenderer2, so we can't support this.
  // TODO(dalesat): Change SetAudioRenderer so it takes an AudioRenderer2.
  FXL_NOTIMPLEMENTED();
}

void MediaPlayerImpl::AddBinding(fidl::InterfaceRequest<MediaPlayer> request) {
  FXL_DCHECK(request);
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace media_player
