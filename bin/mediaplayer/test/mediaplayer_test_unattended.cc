// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include "garnet/bin/mediaplayer/test/fakes/fake_audio.h"
#include "garnet/bin/mediaplayer/test/fakes/fake_wav_reader.h"
#include "garnet/bin/mediaplayer/test/sink_feeder.h"
#include "lib/component/cpp/testing/test_util.h"
#include "lib/component/cpp/testing/test_with_environment.h"
#include "lib/fxl/logging.h"

namespace media_player {
namespace test {

static constexpr uint16_t kSamplesPerFrame = 2;      // Stereo
static constexpr uint32_t kFramesPerSecond = 48000;  // 48kHz
static constexpr size_t kSinkFeedSize = 65536;
static constexpr uint32_t kSinkFeedMaxPacketSize = 4096;
static constexpr uint32_t kSinkFeedMaxPacketCount = 10;

// Base class for mediaplayer tests.
class MediaPlayerTestUnattended
    : public component::testing::TestWithEnvironment {
 protected:
  void SetUp() override {
    auto services = CreateServices();

    // Add the service under test using its launch info.
    fuchsia::sys::LaunchInfo launch_info{
        "fuchsia-pkg://fuchsia.com/mediaplayer#meta/mediaplayer.cmx"};
    zx_status_t status = services->AddServiceWithLaunchInfo(
        std::move(launch_info), fuchsia::mediaplayer::Player::Name_);
    EXPECT_EQ(ZX_OK, status);

    services->AddService(fake_audio_.GetRequestHandler());

    // Create the synthetic environment.
    environment_ =
        CreateNewEnclosingEnvironment("mediaplayer_tests", std::move(services));

    // Instantiate the player under test.
    environment_->ConnectToService(player_.NewRequest());

    player_.set_error_handler([this](zx_status_t status) {
      FXL_LOG(ERROR) << "Player connection closed.";
      player_connection_closed_ = true;
      QuitLoop();
    });
  }

  void TearDown() override { EXPECT_FALSE(player_connection_closed_); }

  fuchsia::mediaplayer::PlayerPtr player_;
  bool player_connection_closed_ = false;

  FakeWavReader fake_reader_;
  FakeAudio fake_audio_;
  std::unique_ptr<component::testing::EnclosingEnvironment> environment_;
  bool sink_connection_closed_ = false;
  SinkFeeder sink_feeder_;
};

// Play a synthetic WAV file from beginning to end.
TEST_F(MediaPlayerTestUnattended, PlayWav) {
  player_.events().OnStatusChanged =
      [this](fuchsia::mediaplayer::PlayerStatus status) {
        if (status.end_of_stream) {
          EXPECT_TRUE(status.ready);
          EXPECT_TRUE(fake_audio_.renderer().expected());
          QuitLoop();
        }
      };

  fake_audio_.renderer().SetPtsUnits(kFramesPerSecond, 1);

  fake_audio_.renderer().ExpectPackets({{0, 4096, 0x20c39d1e31991800},
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

  fuchsia::mediaplayer::SourcePtr source;
  player_->CreateReaderSource(std::move(fake_reader_ptr), source.NewRequest());
  player_->SetSource(std::move(source));

  player_->Play();

  EXPECT_FALSE(RunLoopWithTimeout(zx::duration(ZX_SEC(10))));
}

// Play an LPCM elementary stream using |StreamSource|
TEST_F(MediaPlayerTestUnattended, StreamSource) {
  player_.events().OnStatusChanged =
      [this](fuchsia::mediaplayer::PlayerStatus status) {
        if (status.end_of_stream) {
          EXPECT_TRUE(status.ready);
          EXPECT_TRUE(fake_audio_.renderer().expected());
          QuitLoop();
        }
      };

  fake_audio_.renderer().SetPtsUnits(kFramesPerSecond, 1);

  fake_audio_.renderer().ExpectPackets({{0, 4096, 0xd2fbd957e3bf0000},
                                      {1024, 4096, 0xda25db3fa3bf0000},
                                      {2048, 4096, 0xe227e0f6e3bf0000},
                                      {3072, 4096, 0xe951e2dea3bf0000},
                                      {4096, 4096, 0x37ebf7d3e3bf0000},
                                      {5120, 4096, 0x3f15f9bba3bf0000},
                                      {6144, 4096, 0x4717ff72e3bf0000},
                                      {7168, 4096, 0x4e42015aa3bf0000},
                                      {8192, 4096, 0xeabc5347e3bf0000},
                                      {9216, 4096, 0xf1e6552fa3bf0000},
                                      {10240, 4096, 0xf9e85ae6e3bf0000},
                                      {11264, 4096, 0x01125ccea3bf0000},
                                      {12288, 4096, 0x4fac71c3e3bf0000},
                                      {13312, 4096, 0x56d673aba3bf0000},
                                      {14336, 4096, 0x5ed87962e3bf0000},
                                      {15360, 4096, 0x66027b4aa3bf0000}});

  fuchsia::mediaplayer::StreamSourcePtr stream_source;
  player_->CreateStreamSource(0, false, false, nullptr,
                              stream_source.NewRequest());

  fuchsia::media::AudioStreamType audio_stream_type;
  audio_stream_type.sample_format =
      fuchsia::media::AudioSampleFormat::SIGNED_16;
  audio_stream_type.channels = kSamplesPerFrame;
  audio_stream_type.frames_per_second = kFramesPerSecond;
  fuchsia::media::StreamType stream_type;
  stream_type.medium_specific.set_audio(std::move(audio_stream_type));
  stream_type.encoding = fuchsia::media::AUDIO_ENCODING_LPCM;

  fuchsia::media::SimpleStreamSinkPtr sink;
  stream_source->AddStream(std::move(stream_type), kFramesPerSecond, 1,
                           sink.NewRequest());
  sink.set_error_handler([this]() {
    FXL_LOG(ERROR) << "SimpleStreamSink connection closed.";
    sink_connection_closed_ = true;
    QuitLoop();
  });

  // Here we're upcasting from a
  // |fidl::InterfaceHandle<fuchsia::mediaplayer::StreamSource>| to a
  // |fidl::InterfaceHandle<fuchsia::mediaplayer::Source>| the only way we
  // currently can. The compiler has no way of knowing whether this is
  // legit.
  // TODO(dalesat): Do this safely once FIDL-329 is fixed.
  player_->SetSource(fidl::InterfaceHandle<fuchsia::mediaplayer::Source>(
      stream_source.Unbind().TakeChannel()));

  sink_feeder_.Init(std::move(sink), kSinkFeedSize,
                    kSamplesPerFrame * sizeof(int16_t), kSinkFeedMaxPacketSize,
                    kSinkFeedMaxPacketCount);

  player_->Play();

  EXPECT_FALSE(RunLoopWithTimeout(zx::duration(ZX_SEC(10))));
  EXPECT_FALSE(sink_connection_closed_);
}

}  // namespace test
}  // namespace media_player
