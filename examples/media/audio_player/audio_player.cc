// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/audio_player/audio_player.h"

#include <iomanip>

#include "application/lib/app/connect.h"
#include "apps/media/examples/audio_player/audio_player_params.h"
#include "apps/media/lib/timeline/timeline.h"
#include "apps/media/services/audio_renderer.fidl.h"
#include "apps/media/services/media_service.fidl.h"
#include "apps/media/services/net_media_service.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace examples {

AudioPlayer::AudioPlayer(const AudioPlayerParams& params) {
  FTL_DCHECK(params.is_valid());

  std::unique_ptr<app::ApplicationContext> application_context =
      modular::ApplicationContext::CreateFromStartupInfo();

  media::MediaServicePtr media_service =
      application_context->ConnectToEnvironmentService<media::MediaService>();

  media::NetMediaServicePtr net_media_service =
      application_context
          ->ConnectToEnvironmentService<media::NetMediaService>();

  // Get an audio renderer.
  media::AudioRendererPtr audio_renderer;
  media::MediaRendererPtr audio_media_renderer;
  media_service->CreateAudioRenderer(audio_renderer.NewRequest(),
                                     audio_media_renderer.NewRequest());

  media::MediaPlayerPtr media_player;
  media_service->CreatePlayer(nullptr, std::move(audio_media_renderer), nullptr,
                              media_player.NewRequest());

  net_media_service->CreateNetMediaPlayer(
      params.service_name().empty() ? "audio_player" : params.service_name(),
      std::move(media_player), net_media_player_.NewRequest());

  if (!params.url().empty()) {
    net_media_player_->SetUrl(params.url());
    net_media_player_->Play();
  }

  HandleStatusUpdates();
}

AudioPlayer::~AudioPlayer() {}

void AudioPlayer::HandleStatusUpdates(uint64_t version,
                                      media::MediaPlayerStatusPtr status) {
  if (status) {
    // Process status received from the player.
    if (status->end_of_stream) {
      mtl::MessageLoop::GetCurrent()->PostQuitTask();
    }

    if (status->problem) {
      if (!problem_shown_) {
        FTL_DLOG(INFO) << "PROBLEM: " << status->problem->type << ", "
                       << status->problem->details;
        problem_shown_ = true;
      }
    } else {
      problem_shown_ = false;
    }

    if (status->metadata && !metadata_shown_) {
      FTL_LOG(INFO) << "duration   " << std::fixed << std::setprecision(1)
                    << double(status->metadata->duration) / 1000000000.0
                    << " seconds";
      if (status->metadata->title) {
        FTL_LOG(INFO) << "title      " << status->metadata->title;
      }
      if (status->metadata->artist) {
        FTL_LOG(INFO) << "artist     " << status->metadata->artist;
      }
      if (status->metadata->album) {
        FTL_LOG(INFO) << "album      " << status->metadata->album;
      }
      if (status->metadata->publisher) {
        FTL_LOG(INFO) << "publisher  " << status->metadata->publisher;
      }
      if (status->metadata->genre) {
        FTL_LOG(INFO) << "genre      " << status->metadata->genre;
      }
      if (status->metadata->composer) {
        FTL_LOG(INFO) << "composer   " << status->metadata->composer;
      }
      metadata_shown_ = true;
    }
  }

  // Request a status update.
  net_media_player_->GetStatus(
      version, [this](uint64_t version, media::MediaPlayerStatusPtr status) {
        HandleStatusUpdates(version, std::move(status));
      });
}

}  // namespace examples
