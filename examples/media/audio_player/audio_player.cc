// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/audio_player/audio_player.h"

#include <iomanip>

#include <fcntl.h>
#include <media/cpp/fidl.h>
#include <lib/async-loop/loop.h>
#include <lib/async/default.h>

#include "garnet/examples/media/audio_player/audio_player_params.h"
#include "lib/app/cpp/connect.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/io/fd.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"
#include "lib/url/gurl.h"

using media_player::MediaPlayer;
using media_player::MediaPlayerStatus;
using media_player::MediaPlayerStatusPtr;
using media_player::NetMediaService;

namespace examples {

AudioPlayer::AudioPlayer(const AudioPlayerParams& params,
                         fxl::Closure quit_callback)
    : quit_callback_(quit_callback), quit_when_done_(!params.stay()) {
  FXL_DCHECK(params.is_valid());
  FXL_DCHECK(quit_callback_);

  auto application_context =
      component::ApplicationContext::CreateFromStartupInfo();

  media_player_ =
      application_context->ConnectToEnvironmentService<MediaPlayer>();
  media_player_.events().StatusChanged = [this](MediaPlayerStatus status) {
    HandleStatusChanged(status);
  };

  if (!params.service_name().empty()) {
    auto net_media_service =
        application_context->ConnectToEnvironmentService<NetMediaService>();

    fidl::InterfaceHandle<MediaPlayer> media_player_handle;
    media_player_->AddBinding(media_player_handle.NewRequest());

    net_media_service->PublishMediaPlayer(params.service_name(),
                                          std::move(media_player_handle));
  }

  if (!params.url().empty()) {
    url::GURL url = url::GURL(params.url());

    if (url.SchemeIsFile()) {
      media_player_->SetFileSource(fsl::CloneChannelFromFileDescriptor(
          fxl::UniqueFD(open(url.path().c_str(), O_RDONLY)).get()));
    } else {
      media_player_->SetHttpSource(params.url());
    }

    media_player_->Play();
  }
}

AudioPlayer::~AudioPlayer() {}

void AudioPlayer::HandleStatusChanged(
    const media_player::MediaPlayerStatus& status) {
  // Process status received from the player.
  if (status.end_of_stream && quit_when_done_) {
    quit_callback_();
    FXL_LOG(INFO) << "Reached end-of-stream. Quitting.";
  }

  if (status.problem) {
    if (!problem_shown_) {
      FXL_DLOG(INFO) << "PROBLEM: " << status.problem->type << ", "
                     << status.problem->details;
      problem_shown_ = true;
      if (quit_when_done_) {
        quit_callback_();
        FXL_LOG(INFO) << "Problem detected. Quitting.";
      }
    }
  } else {
    problem_shown_ = false;
  }

  if (status.metadata && !metadata_shown_) {
    FXL_LOG(INFO) << "duration   " << std::fixed << std::setprecision(1)
                  << double(status.metadata->duration) / 1000000000.0
                  << " seconds";
    if (status.metadata->title) {
      FXL_LOG(INFO) << "title      " << status.metadata->title;
    }
    if (status.metadata->artist) {
      FXL_LOG(INFO) << "artist     " << status.metadata->artist;
    }
    if (status.metadata->album) {
      FXL_LOG(INFO) << "album      " << status.metadata->album;
    }
    if (status.metadata->publisher) {
      FXL_LOG(INFO) << "publisher  " << status.metadata->publisher;
    }
    if (status.metadata->genre) {
      FXL_LOG(INFO) << "genre      " << status.metadata->genre;
    }
    if (status.metadata->composer) {
      FXL_LOG(INFO) << "composer   " << status.metadata->composer;
    }
    metadata_shown_ = true;
  }
}

}  // namespace examples
