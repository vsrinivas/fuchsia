// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/audio_player/audio_player.h"

#include <iomanip>

#include "lib/app/cpp/connect.h"
#include "garnet/examples/media/audio_player/audio_player_params.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/fidl/audio_renderer.fidl.h"
#include "lib/media/fidl/media_service.fidl.h"
#include "lib/media/fidl/net_media_service.fidl.h"
#include "lib/fxl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace examples {

AudioPlayer::AudioPlayer(const AudioPlayerParams& params)
    : quit_when_done_(!params.stay()) {
  FXL_DCHECK(params.is_valid());

  auto application_context = app::ApplicationContext::CreateFromStartupInfo();

  auto media_service =
      application_context->ConnectToEnvironmentService<media::MediaService>();

  auto net_media_service =
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
    if (status->end_of_stream && quit_when_done_) {
      mtl::MessageLoop::GetCurrent()->PostQuitTask();
      FXL_LOG(INFO) << "Reached end-of-stream. Quitting.";
    }

    if (status->problem) {
      if (!problem_shown_) {
        FXL_DLOG(INFO) << "PROBLEM: " << status->problem->type << ", "
                       << status->problem->details;
        problem_shown_ = true;
        if (quit_when_done_) {
          mtl::MessageLoop::GetCurrent()->PostQuitTask();
          FXL_LOG(INFO) << "Problem detected. Quitting.";
        }
      }
    } else {
      problem_shown_ = false;
    }

    if (status->metadata && !metadata_shown_) {
      FXL_LOG(INFO) << "duration   " << std::fixed << std::setprecision(1)
                    << double(status->metadata->duration) / 1000000000.0
                    << " seconds";
      if (status->metadata->title) {
        FXL_LOG(INFO) << "title      " << status->metadata->title;
      }
      if (status->metadata->artist) {
        FXL_LOG(INFO) << "artist     " << status->metadata->artist;
      }
      if (status->metadata->album) {
        FXL_LOG(INFO) << "album      " << status->metadata->album;
      }
      if (status->metadata->publisher) {
        FXL_LOG(INFO) << "publisher  " << status->metadata->publisher;
      }
      if (status->metadata->genre) {
        FXL_LOG(INFO) << "genre      " << status->metadata->genre;
      }
      if (status->metadata->composer) {
        FXL_LOG(INFO) << "composer   " << status->metadata->composer;
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
