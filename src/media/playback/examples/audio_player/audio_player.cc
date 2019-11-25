// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/examples/audio_player/audio_player.h"

#include <fcntl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <poll.h>
#include <unistd.h>

#include <iomanip>

#include "lib/fidl/cpp/optional.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/url/gurl.h"
#include "src/media/playback/examples/audio_player/audio_player_params.h"

namespace examples {

AudioPlayer::AudioPlayer(const AudioPlayerParams& params, fit::closure quit_callback)
    : quit_callback_(std::move(quit_callback)), quit_when_done_(!params.stay()) {
  FXL_DCHECK(params.is_valid());
  FXL_DCHECK(quit_callback_);

  auto startup_context = sys::ComponentContext::Create();

  player_ = startup_context->svc()->Connect<fuchsia::media::playback::Player>();
  player_.events().OnStatusChanged = [this](fuchsia::media::playback::PlayerStatus status) {
    HandleStatusChanged(status);
  };

  if (!params.url().empty()) {
    url::GURL url = url::GURL(params.url());

    if (url.SchemeIsFile()) {
      player_->SetFileSource(fsl::CloneChannelFromFileDescriptor(
          fbl::unique_fd(open(url.path().c_str(), O_RDONLY)).get()));
    } else {
      player_->SetHttpSource(params.url(), nullptr);
    }
    GetKeystroke();

    player_->Play();
  }
}

AudioPlayer::~AudioPlayer() {}

void AudioPlayer::HandleStatusChanged(const fuchsia::media::playback::PlayerStatus& status) {
  // Process status received from the player.
  if (status.end_of_stream && quit_when_done_) {
    quit_callback_();
    FXL_LOG(INFO) << "Reached end-of-stream. Quitting.";
  }

  if (status.problem) {
    if (!problem_shown_) {
      FXL_DLOG(INFO) << "PROBLEM: " << status.problem->type << ", " << status.problem->details;
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
                  << double(status.duration) / 1000000000.0 << " seconds";
    MaybeLogMetadataProperty(*status.metadata, fuchsia::media::METADATA_LABEL_TITLE, "title      ");
    MaybeLogMetadataProperty(*status.metadata, fuchsia::media::METADATA_LABEL_ARTIST,
                             "artist     ");
    MaybeLogMetadataProperty(*status.metadata, fuchsia::media::METADATA_LABEL_ALBUM, "album      ");
    MaybeLogMetadataProperty(*status.metadata, fuchsia::media::METADATA_LABEL_PUBLISHER,
                             "publisher  ");
    MaybeLogMetadataProperty(*status.metadata, fuchsia::media::METADATA_LABEL_GENRE, "genre      ");
    MaybeLogMetadataProperty(*status.metadata, fuchsia::media::METADATA_LABEL_COMPOSER,
                             "composer   ");
    metadata_shown_ = true;
  }
}

void AudioPlayer::HandleKeystroke(zx_status_t status, uint32_t events) {
  if (status != ZX_OK) {
    printf("Bad status in HandleKeystroke (status %d)\n", status);
    quit_callback_();
  }

  char c;
  ssize_t res = ::read(STDIN_FILENO, &c, sizeof(c));
  if (res != 1) {
    printf("Error reading keystroke (res %zd, errno %d)\n", res, errno);
    quit_callback_();
  }

  switch (c) {
    case 'q':
    case 'Q':
      quit_callback_();
      break;
    default:
      printf("q - Quit\n");
      break;
  }

  GetKeystroke();
}

void AudioPlayer::GetKeystroke() {
  keystroke_waiter_.Wait([this](zx_status_t s, uint32_t e) { HandleKeystroke(s, e); }, STDIN_FILENO,
                         POLLIN);
}

void AudioPlayer::MaybeLogMetadataProperty(const fuchsia::media::Metadata& metadata,
                                           const std::string& property_label,
                                           const std::string& prefix) {
  for (auto& property : metadata.properties) {
    if (property.label == property_label) {
      FXL_LOG(INFO) << prefix << property.value;
      return;
    }
  }
}

}  // namespace examples
