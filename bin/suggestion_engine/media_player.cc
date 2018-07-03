// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/media_player.h"

#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/media/timeline/timeline.h>
#include <lib/media/timeline/timeline_rate.h>

namespace modular {

MediaPlayer::MediaPlayer(fuchsia::media::AudioPtr audio,
                         std::shared_ptr<SuggestionDebugImpl> debug)
    : audio_(std::move(audio)), debug_(debug) {
  audio_.set_error_handler([this] {
    // TODO(miguelfrde): better error handling. If we observe this message it
    // means that the underlying channel was closed.
    FXL_LOG(WARNING) << "Audio service connection error";
    audio_ = nullptr;
    media_packet_producer_ = nullptr;
  });
}

MediaPlayer::~MediaPlayer() = default;

void MediaPlayer::SetSpeechStatusCallback(SpeechStatusCallback callback) {
  speech_status_callback_ = std::move(callback);
}

void MediaPlayer::PlayMediaResponse(
    fuchsia::modular::MediaResponsePtr media_response) {
  if (!audio_) {
    FXL_LOG(ERROR) << "Not playing query media response because our connection "
                   << "to the Audio service died earlier.";
    return;
  }

  auto activity = debug_->GetIdleWaiter()->RegisterOngoingActivity();

  fuchsia::media::AudioRendererPtr audio_renderer;
  audio_->CreateRenderer(audio_renderer.NewRequest(),
                         media_renderer_.NewRequest());

  media_packet_producer_ = media_response->media_packet_producer.Bind();
  media_renderer_->SetMediaType(std::move(media_response->media_type));
  fuchsia::media::MediaPacketConsumerPtr consumer;
  media_renderer_->GetPacketConsumer(consumer.NewRequest());

  media_packet_producer_->Connect(std::move(consumer), [this, activity] {
    OnMediaPacketProducerConnected(activity);
  });

  media_packet_producer_.set_error_handler([this] {
    speech_status_callback_(fuchsia::modular::SpeechStatus::IDLE);
  });
}

void MediaPlayer::OnMediaPacketProducerConnected(
    util::IdleWaiter::ActivityToken activity) {
  time_lord_.Unbind();
  media_timeline_consumer_.Unbind();

  speech_status_callback_(fuchsia::modular::SpeechStatus::RESPONDING);

  media_renderer_->GetTimelineControlPoint(time_lord_.NewRequest());
  time_lord_->GetTimelineConsumer(media_timeline_consumer_.NewRequest());
  time_lord_->Prime([this, activity] {
    fuchsia::media::TimelineTransform tt;
    tt.reference_time =
        ::media::Timeline::local_now() + ::media::Timeline::ns_from_ms(30);
    tt.subject_time = fuchsia::media::kUnspecifiedTime;
    tt.reference_delta = tt.subject_delta = 1;

    HandleMediaUpdates(fuchsia::media::kInitialStatus, nullptr);

    media_timeline_consumer_->SetTimelineTransform(
        std::move(tt), [activity](bool completed) {});
  });
}

void MediaPlayer::HandleMediaUpdates(
    uint64_t version,
    fuchsia::media::MediaTimelineControlPointStatusPtr status) {
  auto activity = debug_->GetIdleWaiter()->RegisterOngoingActivity();

  if (status && status->end_of_stream) {
    media_renderer_ = nullptr;
  } else {
    time_lord_->GetStatus(
        version,
        [this, activity](
            uint64_t next_version,
            fuchsia::media::MediaTimelineControlPointStatus next_status) {
          HandleMediaUpdates(next_version,
                             fidl::MakeOptional(std::move(next_status)));
        });
  }
}

}  // namespace modular
