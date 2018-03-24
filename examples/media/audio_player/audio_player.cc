// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/audio_player/audio_player.h"

#include <fcntl.h>

#include <iomanip>

#include <fuchsia/cpp/media.h>

#include "garnet/bin/media/util/file_channel.h"
#include "garnet/examples/media/audio_player/audio_player_params.h"
#include "lib/app/cpp/connect.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"
#include "lib/url/gurl.h"

namespace examples {

AudioPlayer::AudioPlayer(const AudioPlayerParams& params)
    : quit_when_done_(!params.stay()) {
  FXL_DCHECK(params.is_valid());

  auto application_context =
      component::ApplicationContext::CreateFromStartupInfo();

  media_player_ =
      application_context->ConnectToEnvironmentService<media::MediaPlayer>();

  if (!params.service_name().empty()) {
    auto net_media_service =
        application_context
            ->ConnectToEnvironmentService<media::NetMediaService>();

    fidl::InterfaceHandle<media::MediaPlayer> media_player_handle;
    media_player_->AddBinding(media_player_handle.NewRequest());

    net_media_service->PublishMediaPlayer(params.service_name(),
                                          std::move(media_player_handle));
  }

  if (!params.url().empty()) {
    url::GURL url = url::GURL(params.url());

    if (url.SchemeIsFile()) {
      media_player_->SetFileSource(media::ChannelFromFd(
          fxl::UniqueFD(open(url.path().c_str(), O_RDONLY))));
    } else {
      media_player_->SetHttpSource(params.url());
    }

    media_player_->Play();
  }

  HandleStatusUpdates();
}

AudioPlayer::~AudioPlayer() {}

void AudioPlayer::HandleStatusUpdates(uint64_t version,
                                      media::MediaPlayerStatusPtr status) {
  if (status) {
    // Process status received from the player.
    if (status->end_of_stream && quit_when_done_) {
      fsl::MessageLoop::GetCurrent()->PostQuitTask();
      FXL_LOG(INFO) << "Reached end-of-stream. Quitting.";
    }

    if (status->problem) {
      if (!problem_shown_) {
        FXL_DLOG(INFO) << "PROBLEM: " << status->problem->type << ", "
                       << status->problem->details;
        problem_shown_ = true;
        if (quit_when_done_) {
          fsl::MessageLoop::GetCurrent()->PostQuitTask();
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
  media_player_->GetStatus(
      version, [this](uint64_t version, media::MediaPlayerStatus status) {
        HandleStatusUpdates(version, fidl::MakeOptional(std::move(status)));
      });
}

}  // namespace examples
