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
    FXL_LOG(WARNING) << "Audio connection error";
    audio_ = nullptr;
  });
}

MediaPlayer::~MediaPlayer() = default;

void MediaPlayer::SetSpeechStatusCallback(SpeechStatusCallback callback) {
  speech_status_callback_ = std::move(callback);
}

void MediaPlayer::PlayAudioResponse(
    fidl::InterfaceRequest<fuchsia::media::AudioOut> audio_response) {
  if (!audio_) {
    FXL_LOG(ERROR) << "Not playing query audio response because our audio "
                      "service connection died earlier.";
    return;
  }

  audio_->CreateAudioOut(audio_out_.NewRequest());
  audio_out_binding_ =
      std::make_unique<fidl::Binding<fuchsia::media::AudioOut>>(
          audio_out_.get());
  audio_out_binding_->set_error_handler([this] {
    speech_status_callback_(fuchsia::modular::SpeechStatus::IDLE);
  });
  audio_out_binding_->Bind(std::move(audio_response));

  speech_status_callback_(fuchsia::modular::SpeechStatus::RESPONDING);
}

}  // namespace modular
