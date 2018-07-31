// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/audio_player/audio_player.h"

#include <iomanip>

#include <fcntl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/async-loop/loop.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>

#include "garnet/examples/media/audio_player/audio_player_params.h"
#include "lib/component/cpp/connect.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/io/fd.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"
#include "lib/url/gurl.h"

namespace examples {

AudioPlayer::AudioPlayer(const AudioPlayerParams& params,
                         fit::closure quit_callback)
    : quit_callback_(std::move(quit_callback)),
      quit_when_done_(!params.stay()) {
  FXL_DCHECK(params.is_valid());
  FXL_DCHECK(quit_callback_);

  auto startup_context = component::StartupContext::CreateFromStartupInfo();

  media_player_ =
      startup_context
          ->ConnectToEnvironmentService<fuchsia::mediaplayer::MediaPlayer>();
  media_player_.events().StatusChanged =
      [this](fuchsia::mediaplayer::MediaPlayerStatus status) {
        HandleStatusChanged(status);
      };

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
    const fuchsia::mediaplayer::MediaPlayerStatus& status) {
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
                  << double(status.duration_ns) / 1000000000.0 << " seconds";
    MaybeLogMetadataProperty(*status.metadata,
                             fuchsia::mediaplayer::METADATA_LABEL_TITLE,
                             "title      ");
    MaybeLogMetadataProperty(*status.metadata,
                             fuchsia::mediaplayer::METADATA_LABEL_ARTIST,
                             "artist     ");
    MaybeLogMetadataProperty(*status.metadata,
                             fuchsia::mediaplayer::METADATA_LABEL_ALBUM,
                             "album      ");
    MaybeLogMetadataProperty(*status.metadata,
                             fuchsia::mediaplayer::METADATA_LABEL_PUBLISHER,
                             "publisher  ");
    MaybeLogMetadataProperty(*status.metadata,
                             fuchsia::mediaplayer::METADATA_LABEL_GENRE,
                             "genre      ");
    MaybeLogMetadataProperty(*status.metadata,
                             fuchsia::mediaplayer::METADATA_LABEL_COMPOSER,
                             "composer   ");
    metadata_shown_ = true;
  }
}

void AudioPlayer::MaybeLogMetadataProperty(
    const fuchsia::mediaplayer::Metadata& metadata,
    const std::string& property_label, const std::string& prefix) {
  for (auto& property : *metadata.properties) {
    if (property.label == property_label) {
      FXL_LOG(INFO) << prefix << property.value;
      return;
    }
  }
}

}  // namespace examples
