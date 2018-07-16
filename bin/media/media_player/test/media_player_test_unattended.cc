// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/test/media_player_test_unattended.h"

#include <iostream>

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include "garnet/bin/media/media_player/test/fakes/fake_audio_renderer.h"
#include "garnet/bin/media/media_player/test/fakes/fake_wav_reader.h"
#include "lib/component/cpp/connect.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media_player {
namespace test {

MediaPlayerTestUnattended::MediaPlayerTestUnattended(
    fit::function<void(int)> quit_callback)
    : startup_context_(component::StartupContext::CreateFromStartupInfo()),
      quit_callback_(std::move(quit_callback)) {
  FXL_DCHECK(quit_callback_);
  std::cerr << "MediaPlayerTest starting\n";

  media_player_ =
      startup_context_
          ->ConnectToEnvironmentService<fuchsia::mediaplayer::MediaPlayer>();
  media_player_.events().StatusChanged =
      [this](fuchsia::mediaplayer::MediaPlayerStatus status) {
        if (status.end_of_stream) {
          FXL_LOG(INFO) << "MediaPlayerTest "
                        << (fake_audio_renderer_.expected() ? "SUCCEEDED"
                                                            : "FAILED");
          quit_callback_(fake_audio_renderer_.expected() ? 0 : 1);
        }
      };

  fake_audio_renderer_.SetPtsUnits(48000, 1);

  fake_audio_renderer_.ExpectPackets({{0, 4096, 0x20c39d1e31991800},
                                      {1024, 4096, 0xeaf137125d313800},
                                      {2048, 4096, 0x6162095671991800},
                                      {3072, 4096, 0x36e551c7dd41f800},
                                      {4096, 4096, 0x23dcbf6fb1991800},
                                      {5120, 4096, 0xee0a5963dd313800},
                                      {6144, 4096, 0x647b2ba7f1991800},
                                      {7168, 4096, 0x39fe74195d41f800},
                                      {8192, 4096, 0xb3de76b931991800},
                                      {9216, 4096, 0x7e0c10ad5d313800},
                                      {10240, 4096, 0xf47ce2f171991800},
                                      {11264, 4096, 0xca002b62dd41f800},
                                      {12288, 4096, 0xb6f7990ab1991800},
                                      {13312, 4096, 0x812532fedd313800},
                                      {14336, 4096, 0xf7960542f1991800},
                                      {15360, 4052, 0x7308a9824acbd5ea}});

  fuchsia::mediaplayer::SeekingReaderPtr fake_reader_ptr;
  fidl::InterfaceRequest<fuchsia::mediaplayer::SeekingReader> reader_request =
      fake_reader_ptr.NewRequest();
  fake_reader_.Bind(std::move(reader_request));

  fuchsia::media::AudioRenderer2Ptr fake_audio_renderer_ptr;
  fake_audio_renderer_.Bind(fake_audio_renderer_ptr.NewRequest());

  media_player_->SetAudioRenderer(std::move(fake_audio_renderer_ptr));

  media_player_->SetReaderSource(std::move(fake_reader_ptr));

  media_player_->Play();
}

}  // namespace test
}  // namespace media_player
