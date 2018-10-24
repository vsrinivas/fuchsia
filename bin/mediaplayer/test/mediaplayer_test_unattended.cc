// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/real_loop_fixture.h>
#include <iostream>
#include "garnet/bin/mediaplayer/test/fakes/fake_audio_renderer.h"
#include "garnet/bin/mediaplayer/test/fakes/fake_wav_reader.h"
#include "lib/component/cpp/environment_services_helper.h"
#include "lib/fxl/logging.h"

namespace media_player {
namespace test {

static constexpr uint32_t kFramesPerSecond = 48000;  // 48kHz

// Base class for mediaplayer tests.
class MediaPlayerTestUnattended : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    environment_services_ = component::GetEnvironmentServices();
    environment_services_->ConnectToService(player_.NewRequest());

    player_.set_error_handler([this]() {
      FXL_LOG(ERROR) << "Player connection closed.";
      player_connection_closed_ = true;
      QuitLoop();
    });
  }

  void TearDown() override { EXPECT_FALSE(player_connection_closed_); }

  std::shared_ptr<component::Services> environment_services_;
  fuchsia::mediaplayer::PlayerPtr player_;
  bool player_connection_closed_ = false;

  FakeWavReader fake_reader_;
  FakeAudioRenderer fake_audio_renderer_;
};

// Play a synthetic WAV file from beginning to end.
TEST_F(MediaPlayerTestUnattended, PlayWav) {
  player_.events().OnStatusChanged =
      [this](fuchsia::mediaplayer::PlayerStatus status) {
        if (status.end_of_stream) {
          EXPECT_TRUE(status.ready);
          EXPECT_TRUE(fake_audio_renderer_.expected());
          QuitLoop();
        }
      };

  fake_audio_renderer_.SetPtsUnits(kFramesPerSecond, 1);

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

  fuchsia::media::AudioRendererPtr fake_audio_renderer_ptr;
  fake_audio_renderer_.Bind(fake_audio_renderer_ptr.NewRequest());

  player_->SetAudioRenderer(std::move(fake_audio_renderer_ptr));

  player_->SetReaderSource(std::move(fake_reader_ptr));

  player_->Play();

  EXPECT_FALSE(RunLoopWithTimeout(zx::duration(ZX_SEC(10))));
}

}  // namespace test
}  // namespace media_player
