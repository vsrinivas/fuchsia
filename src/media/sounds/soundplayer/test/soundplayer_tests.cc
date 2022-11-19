// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include "src/lib/fsl/io/fd.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/media/sounds/soundplayer/sound_player_impl.h"
#include "src/media/sounds/soundplayer/test/fake_audio_renderer.h"

namespace soundplayer {
namespace test {

constexpr uint64_t kPayloadSize = 1024;
constexpr uint32_t kFrameSize = 2;
constexpr uint32_t kFramesPerSecond = 44100;

constexpr uint64_t kWavFilePayloadSize = 25438;
constexpr int64_t kWavFileDuration = 288412698;
constexpr uint32_t kWavFileChannels = 1;
constexpr uint32_t kWavFramesPerSecond = 44100;

constexpr uint64_t kOggOpusFilePayloadSize = 530592;
constexpr int64_t kOggOpusFileDuration = 2763500000;
constexpr uint32_t kOggOpusFileChannels = 2;
constexpr uint32_t kOggOpusFramesPerSecond = 48000;

constexpr fuchsia::media::AudioRenderUsage kUsage = fuchsia::media::AudioRenderUsage::MEDIA;

zx_koid_t GetKoid(const zx::vmo& vmo) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(vmo.get(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  FX_CHECK(status == ZX_OK);
  return info.koid;
}

class FakeAudio : public fuchsia::media::Audio {
 public:
  FakeAudio() : binding_(this) {}

  fuchsia::media::AudioPtr NewPtr() { return binding_.NewBinding().Bind(); }

  // fuchsia::media::Audio implementation.
  void CreateAudioRenderer(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request) override {
    EXPECT_FALSE(expectations_.empty());
    EXPECT_FALSE(expecations_iter_ == expectations_.end());

    auto renderer = std::make_unique<FakeAudioRenderer>();
    if (warmup_renderer_created_) {
      renderer->SetExpectations(*expecations_iter_);
      ++expecations_iter_;
    } else {
      renderer->ExpectWarmup(block_warmup_);
      warmup_renderer_created_ = true;
    }

    auto raw_renderer = renderer.get();
    renderer->Bind(std::move(request), [this, raw_renderer](zx_status_t status) {
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, status);
      EXPECT_TRUE(raw_renderer->completed());
      renderers_.erase(raw_renderer);
    });
    renderers_.emplace(raw_renderer, std::move(renderer));
  }

  void CreateAudioCapturer(fidl::InterfaceRequest<fuchsia::media::AudioCapturer> request,
                           bool loopback) override {
    FX_NOTIMPLEMENTED();
  }

  // Prevents warmup from completing until |ChangeMinLeadTime| is called with a non-zero
  // duration.
  void SetBlockWarmup() { block_warmup_ = true; }

  // Sets expectations for renderers.
  void SetRendererExpectations(const std::vector<FakeAudioRenderer::Expectations>& expectations) {
    expectations_ = expectations;
    expecations_iter_ = expectations_.begin();
  }

  void ChangeMinLeadTime(zx::duration min_lead_time) {
    for (auto& pair : renderers_) {
      pair.second->ChangeMinLeadTime(min_lead_time);
    }
  }

  bool renderers_completed() const { return renderers_.empty(); }

 private:
  fidl::Binding<fuchsia::media::Audio> binding_;
  std::unordered_map<FakeAudioRenderer*, std::unique_ptr<FakeAudioRenderer>> renderers_;
  std::vector<FakeAudioRenderer::Expectations> expectations_;
  std::vector<FakeAudioRenderer::Expectations>::iterator expecations_iter_;
  bool warmup_renderer_created_ = false;
  bool block_warmup_ = false;
};

class SoundPlayerTests : public gtest::RealLoopFixture {
 public:
  SoundPlayerTests()
      : ptr_to_under_test_(), under_test_(fake_audio_.NewPtr(), ptr_to_under_test_.NewRequest()) {}

 protected:
  // Prevents warmup from completing until |ChangeMinLeadTime| is called with a non-zero
  // duration.
  void SetBlockWarmup() { fake_audio_.SetBlockWarmup(); }

  // Sets expectations for renderers.
  void SetRendererExpectations(const std::vector<FakeAudioRenderer::Expectations>& expectations) {
    fake_audio_.SetRendererExpectations(expectations);
  }

  void ChangeMinLeadTime(zx::duration min_lead_time) {
    fake_audio_.ChangeMinLeadTime(min_lead_time);
  }

  SoundPlayerImpl& under_test() { return under_test_; }

  fuchsia::media::sounds::PlayerPtr& under_test_ptr() { return ptr_to_under_test_; }

  bool renderers_completed() const { return fake_audio_.renderers_completed(); }

  std::tuple<fuchsia::mem::Buffer, zx_koid_t, fuchsia::media::AudioStreamType> CreateTestSound(
      uint64_t size) {
    FX_CHECK(size % sizeof(int16_t) == 0);
    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(size, 0, &vmo);
    FX_CHECK(status == ZX_OK);
    zx_koid_t koid = GetKoid(vmo);

    return {fuchsia::mem::Buffer{
                .vmo = std::move(vmo),
                .size = size,
            },
            koid,
            fuchsia::media::AudioStreamType{
                .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                .channels = kFrameSize / sizeof(int16_t),
                .frames_per_second = kFramesPerSecond,
            }};
  }

  fidl::InterfaceHandle<fuchsia::io::File> ResourceFile(const std::string& file_name) {
    auto fd = fbl::unique_fd(open(("/pkg/data/" + file_name).c_str(), O_RDONLY));
    EXPECT_TRUE(fd.is_valid());
    return fidl::InterfaceHandle<fuchsia::io::File>(
        fsl::TransferChannelFromFileDescriptor(std::move(fd)));
  }

 private:
  FakeAudio fake_audio_;
  fuchsia::media::sounds::PlayerPtr ptr_to_under_test_;
  SoundPlayerImpl under_test_;
};

// Tests nominal playback of a sound added as a buffer.
TEST_F(SoundPlayerTests, Buffer) {
  auto [buffer, koid, stream_type] = CreateTestSound(kPayloadSize);

  SetRendererExpectations({{
      .payload_buffer_ = koid,
      .packets_ = {{.pts = fuchsia::media::NO_TIMESTAMP,
                    .payload_buffer_id = 0,
                    .payload_offset = 0,
                    .payload_size = kPayloadSize,
                    .flags = 0,
                    .buffer_config = 0,
                    .stream_segment_id = 0}},
      .stream_type_ = fidl::Clone(stream_type),
      .usage_ = kUsage,
      .block_completion_ = false,
  }});

  under_test().AddSoundBuffer(0, std::move(buffer), std::move(stream_type));
  bool play_sound_completed = false;
  under_test().PlaySound(
      0, kUsage, [&play_sound_completed](fuchsia::media::sounds::Player_PlaySound_Result result) {
        EXPECT_TRUE(result.is_response());
        play_sound_completed = true;
      });
  RunLoopUntil([&play_sound_completed]() { return play_sound_completed; });
  RunLoopUntil([this]() { return renderers_completed(); });
  under_test().RemoveSound(0);
  RunLoopUntilIdle();
}

// Plays a sound of the maximum size the renderer will play as a single packet.
TEST_F(SoundPlayerTests, MaxSinglePacketBuffer) {
  auto [buffer, koid, stream_type] =
      CreateTestSound(fuchsia::media::MAX_FRAMES_PER_RENDERER_PACKET * kFrameSize);

  SetRendererExpectations({{
      .payload_buffer_ = koid,
      .packets_ = {{.pts = fuchsia::media::NO_TIMESTAMP,
                    .payload_buffer_id = 0,
                    .payload_offset = 0,
                    .payload_size = fuchsia::media::MAX_FRAMES_PER_RENDERER_PACKET * kFrameSize,
                    .flags = 0,
                    .buffer_config = 0,
                    .stream_segment_id = 0}},
      .stream_type_ = fidl::Clone(stream_type),
      .usage_ = kUsage,
      .block_completion_ = false,
  }});

  under_test().AddSoundBuffer(0, std::move(buffer), std::move(stream_type));
  bool play_sound_completed = false;
  under_test().PlaySound(
      0, kUsage, [&play_sound_completed](fuchsia::media::sounds::Player_PlaySound_Result result) {
        EXPECT_TRUE(result.is_response());
        play_sound_completed = true;
      });
  RunLoopUntil([&play_sound_completed]() { return play_sound_completed; });
  RunLoopUntil([this]() { return renderers_completed(); });
  under_test().RemoveSound(0);
  RunLoopUntilIdle();
}

// Plays a sound large enough to require two renderer packets.
TEST_F(SoundPlayerTests, TwoPacketBuffer) {
  auto [buffer, koid, stream_type] =
      CreateTestSound((fuchsia::media::MAX_FRAMES_PER_RENDERER_PACKET + 1) * kFrameSize);

  SetRendererExpectations({{
      .payload_buffer_ = koid,
      .packets_ = {{.pts = fuchsia::media::NO_TIMESTAMP,
                    .payload_buffer_id = 0,
                    .payload_offset = 0,
                    .payload_size = fuchsia::media::MAX_FRAMES_PER_RENDERER_PACKET * kFrameSize,
                    .flags = 0,
                    .buffer_config = 0,
                    .stream_segment_id = 0},
                   {.pts = fuchsia::media::NO_TIMESTAMP,
                    .payload_buffer_id = 0,
                    .payload_offset = fuchsia::media::MAX_FRAMES_PER_RENDERER_PACKET * kFrameSize,
                    .payload_size = kFrameSize,
                    .flags = 0,
                    .buffer_config = 0,
                    .stream_segment_id = 0}},
      .stream_type_ = fidl::Clone(stream_type),
      .usage_ = kUsage,
      .block_completion_ = false,
  }});

  under_test().AddSoundBuffer(0, std::move(buffer), std::move(stream_type));
  bool play_sound_completed = false;
  under_test().PlaySound(
      0, kUsage, [&play_sound_completed](fuchsia::media::sounds::Player_PlaySound_Result result) {
        EXPECT_TRUE(result.is_response());
        play_sound_completed = true;
      });
  RunLoopUntil([&play_sound_completed]() { return play_sound_completed; });
  RunLoopUntil([this]() { return renderers_completed(); });
  under_test().RemoveSound(0);
  RunLoopUntilIdle();
}

// Plays a sound from a wav file.
TEST_F(SoundPlayerTests, WavFile) {
  SetRendererExpectations({{
      .payload_buffer_ = ZX_KOID_INVALID,
      .packets_ = {{.pts = fuchsia::media::NO_TIMESTAMP,
                    .payload_buffer_id = 0,
                    .payload_offset = 0,
                    .payload_size = kWavFilePayloadSize,
                    .flags = 0,
                    .buffer_config = 0,
                    .stream_segment_id = 0}},
      .stream_type_ =
          {
              .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
              .channels = kWavFileChannels,
              .frames_per_second = kWavFramesPerSecond,
          },
      .usage_ = kUsage,
      .block_completion_ = false,
  }});

  under_test().AddSoundFromFile(0, ResourceFile("sfx.wav"),
                                [](fuchsia::media::sounds::Player_AddSoundFromFile_Result result) {
                                  EXPECT_TRUE(result.is_response());
                                  EXPECT_EQ(kWavFileDuration, result.response().duration);
                                });
  bool play_sound_completed = false;
  under_test().PlaySound(
      0, kUsage, [&play_sound_completed](fuchsia::media::sounds::Player_PlaySound_Result result) {
        EXPECT_TRUE(result.is_response());
        play_sound_completed = true;
      });
  RunLoopUntil([&play_sound_completed]() { return play_sound_completed; });
  RunLoopUntil([this]() { return renderers_completed(); });
  under_test().RemoveSound(0);
  RunLoopUntilIdle();
}

// Plays a sound from a wav file twice.
TEST_F(SoundPlayerTests, WavFileTwice) {
  SetRendererExpectations({
      {
          .payload_buffer_ = ZX_KOID_INVALID,
          .packets_ = {{.pts = fuchsia::media::NO_TIMESTAMP,
                        .payload_buffer_id = 0,
                        .payload_offset = 0,
                        .payload_size = kWavFilePayloadSize,
                        .flags = 0,
                        .buffer_config = 0,
                        .stream_segment_id = 0}},
          .stream_type_ =
              {
                  .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                  .channels = kWavFileChannels,
                  .frames_per_second = kWavFramesPerSecond,
              },
          .usage_ = kUsage,
          .block_completion_ = false,
      },
      {
          .payload_buffer_ = ZX_KOID_INVALID,
          .packets_ = {{.pts = fuchsia::media::NO_TIMESTAMP,
                        .payload_buffer_id = 0,
                        .payload_offset = 0,
                        .payload_size = kWavFilePayloadSize,
                        .flags = 0,
                        .buffer_config = 0,
                        .stream_segment_id = 0}},
          .stream_type_ =
              {
                  .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                  .channels = 1,
                  .frames_per_second = kWavFramesPerSecond,
              },
          .usage_ = kUsage,
          .block_completion_ = false,
      },
  });

  under_test().AddSoundFromFile(0, ResourceFile("sfx.wav"),
                                [](fuchsia::media::sounds::Player_AddSoundFromFile_Result result) {
                                  EXPECT_TRUE(result.is_response());
                                  EXPECT_EQ(kWavFileDuration, result.response().duration);
                                });
  bool play_sound_completed = false;
  under_test().PlaySound(
      0, kUsage, [&play_sound_completed](fuchsia::media::sounds::Player_PlaySound_Result result) {
        EXPECT_TRUE(result.is_response());
        play_sound_completed = true;
      });
  RunLoopUntil([&play_sound_completed]() { return play_sound_completed; });
  RunLoopUntil([this]() { return renderers_completed(); });

  play_sound_completed = false;
  under_test().PlaySound(
      0, kUsage, [&play_sound_completed](fuchsia::media::sounds::Player_PlaySound_Result result) {
        EXPECT_TRUE(result.is_response());
        play_sound_completed = true;
      });
  RunLoopUntil([&play_sound_completed]() { return play_sound_completed; });
  RunLoopUntil([this]() { return renderers_completed(); });

  under_test().RemoveSound(0);
  RunLoopUntilIdle();
}

// Plays and stops a sound from a wav file.
TEST_F(SoundPlayerTests, WavFileStop) {
  SetRendererExpectations({{
      .payload_buffer_ = ZX_KOID_INVALID,
      .packets_ = {{.pts = fuchsia::media::NO_TIMESTAMP,
                    .payload_buffer_id = 0,
                    .payload_offset = 0,
                    .payload_size = kWavFilePayloadSize,
                    .flags = 0,
                    .buffer_config = 0,
                    .stream_segment_id = 0}},
      .stream_type_ =
          {
              .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
              .channels = kWavFileChannels,
              .frames_per_second = kWavFramesPerSecond,
          },
      .usage_ = kUsage,
      .block_completion_ = true,
  }});

  under_test().AddSoundFromFile(0, ResourceFile("sfx.wav"),
                                [](fuchsia::media::sounds::Player_AddSoundFromFile_Result result) {
                                  EXPECT_TRUE(result.is_response());
                                  EXPECT_EQ(kWavFileDuration, result.response().duration);
                                });
  bool play_sound_completed = false;
  under_test().PlaySound(
      0, kUsage, [&play_sound_completed](fuchsia::media::sounds::Player_PlaySound_Result result) {
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(fuchsia::media::sounds::PlaySoundError::STOPPED, result.err());
        play_sound_completed = true;
      });
  RunLoopUntilIdle();

  under_test().StopPlayingSound(0);

  RunLoopUntil([&play_sound_completed]() { return play_sound_completed; });
  RunLoopUntil([this]() { return renderers_completed(); });
  under_test().RemoveSound(0);
  RunLoopUntilIdle();
}

// Plays a sound from a wav file twice.
TEST_F(SoundPlayerTests, WavFileTwiceStopSecond) {
  SetRendererExpectations({
      {
          .payload_buffer_ = ZX_KOID_INVALID,
          .packets_ = {{.pts = fuchsia::media::NO_TIMESTAMP,
                        .payload_buffer_id = 0,
                        .payload_offset = 0,
                        .payload_size = kWavFilePayloadSize,
                        .flags = 0,
                        .buffer_config = 0,
                        .stream_segment_id = 0}},
          .stream_type_ =
              {
                  .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                  .channels = kWavFileChannels,
                  .frames_per_second = kWavFramesPerSecond,
              },
          .usage_ = kUsage,
          .block_completion_ = true,
      },
      {
          .payload_buffer_ = ZX_KOID_INVALID,
          .packets_ = {{.pts = fuchsia::media::NO_TIMESTAMP,
                        .payload_buffer_id = 0,
                        .payload_offset = 0,
                        .payload_size = kWavFilePayloadSize,
                        .flags = 0,
                        .buffer_config = 0,
                        .stream_segment_id = 0}},
          .stream_type_ =
              {
                  .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                  .channels = kWavFileChannels,
                  .frames_per_second = kWavFramesPerSecond,
              },
          .usage_ = kUsage,
          .block_completion_ = true,
      },
  });

  under_test().AddSoundFromFile(0, ResourceFile("sfx.wav"),
                                [](fuchsia::media::sounds::Player_AddSoundFromFile_Result result) {
                                  EXPECT_TRUE(result.is_response());
                                  EXPECT_EQ(kWavFileDuration, result.response().duration);
                                });
  under_test().PlaySound(0, kUsage, [](fuchsia::media::sounds::Player_PlaySound_Result result) {
    // Never completes.
    EXPECT_TRUE(false);
  });
  RunLoopUntilIdle();

  bool second_play_sound_completed = false;
  under_test().PlaySound(
      0, kUsage,
      [&second_play_sound_completed](fuchsia::media::sounds::Player_PlaySound_Result result) {
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(fuchsia::media::sounds::PlaySoundError::STOPPED, result.err());
        second_play_sound_completed = true;
      });
  RunLoopUntilIdle();

  EXPECT_FALSE(second_play_sound_completed);

  // Calling |StopPlayingSound| should only stop the second sound from playing. The first should
  // continue to block.
  under_test().StopPlayingSound(0);
  RunLoopUntil([&second_play_sound_completed]() { return second_play_sound_completed; });

  under_test().RemoveSound(0);
  RunLoopUntilIdle();
}

// Tests that bogus stop requests work (or don't) as expected.
TEST_F(SoundPlayerTests, WavFileBogusStops) {
  SetRendererExpectations({{
      .payload_buffer_ = ZX_KOID_INVALID,
      .packets_ = {{.pts = fuchsia::media::NO_TIMESTAMP,
                    .payload_buffer_id = 0,
                    .payload_offset = 0,
                    .payload_size = kWavFilePayloadSize,
                    .flags = 0,
                    .buffer_config = 0,
                    .stream_segment_id = 0}},
      .stream_type_ =
          {
              .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
              .channels = kWavFileChannels,
              .frames_per_second = kWavFramesPerSecond,
          },
      .usage_ = kUsage,
      .block_completion_ = false,
  }});

  under_test().AddSoundFromFile(0, ResourceFile("sfx.wav"),
                                [](fuchsia::media::sounds::Player_AddSoundFromFile_Result result) {
                                  EXPECT_TRUE(result.is_response());
                                  EXPECT_EQ(kWavFileDuration, result.response().duration);
                                });

  // Stop a sound that hasn't been played.
  under_test().StopPlayingSound(0);

  // Play the sound.
  bool play_sound_completed = false;
  under_test().PlaySound(
      0, kUsage, [&play_sound_completed](fuchsia::media::sounds::Player_PlaySound_Result result) {
        EXPECT_TRUE(result.is_response());
        play_sound_completed = true;
      });
  RunLoopUntil([&play_sound_completed]() { return play_sound_completed; });
  RunLoopUntil([this]() { return renderers_completed(); });

  // Stop a sound that has already completed.
  under_test().StopPlayingSound(0);

  // Stop a sound that doesn't exist.
  under_test().StopPlayingSound(1);

  under_test().RemoveSound(0);
  RunLoopUntilIdle();

  // Stop a sound that no longer exists.
  under_test().StopPlayingSound(0);
  RunLoopUntilIdle();
}

// Plays a sound from an ogg/opus file.
TEST_F(SoundPlayerTests, FileOggOpus) {
  SetRendererExpectations({{
      .payload_buffer_ = ZX_KOID_INVALID,
      .packets_ = {{.pts = fuchsia::media::NO_TIMESTAMP,
                    .payload_buffer_id = 0,
                    .payload_offset = 0,
                    .payload_size = kOggOpusFilePayloadSize,
                    .flags = 0,
                    .buffer_config = 0,
                    .stream_segment_id = 0}},
      .stream_type_ =
          {
              .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
              .channels = kOggOpusFileChannels,
              .frames_per_second = kOggOpusFramesPerSecond,
          },
      .usage_ = kUsage,
      .block_completion_ = false,
  }});

  under_test().AddSoundFromFile(0, ResourceFile("testfile.ogg"),
                                [](fuchsia::media::sounds::Player_AddSoundFromFile_Result result) {
                                  EXPECT_TRUE(result.is_response());
                                  EXPECT_EQ(kOggOpusFileDuration, result.response().duration);
                                });
  bool play_sound_completed = false;
  under_test().PlaySound(
      0, kUsage, [&play_sound_completed](fuchsia::media::sounds::Player_PlaySound_Result result) {
        EXPECT_TRUE(result.is_response());
        play_sound_completed = true;
      });
  RunLoopUntil([&play_sound_completed]() { return play_sound_completed; });
  RunLoopUntil([this]() { return renderers_completed(); });
  under_test().RemoveSound(0);
  RunLoopUntilIdle();
}

// Tests that play is deferred until the audio service is ready.
TEST_F(SoundPlayerTests, WhenReady) {
  auto [buffer, koid, stream_type] = CreateTestSound(kPayloadSize);

  SetBlockWarmup();
  SetRendererExpectations({{
      .payload_buffer_ = koid,
      .packets_ = {{.pts = fuchsia::media::NO_TIMESTAMP,
                    .payload_buffer_id = 0,
                    .payload_offset = 0,
                    .payload_size = kPayloadSize,
                    .flags = 0,
                    .buffer_config = 0,
                    .stream_segment_id = 0}},
      .stream_type_ = fidl::Clone(stream_type),
      .usage_ = kUsage,
      .block_completion_ = false,
  }});

  // We use |under_test_ptr()| here, because invocation of methods is deferred by deferring
  // the channel bind. If we call the implementation directly, we bypass the warmup.
  under_test_ptr()->AddSoundBuffer(0, std::move(buffer), std::move(stream_type));
  bool play_sound_completed = false;
  under_test_ptr()->PlaySound(
      0, kUsage, [&play_sound_completed](fuchsia::media::sounds::Player_PlaySound_Result result) {
        EXPECT_TRUE(result.is_response());
        play_sound_completed = true;
      });
  RunLoopUntilIdle();
  EXPECT_FALSE(play_sound_completed);
  ChangeMinLeadTime(zx::msec(10));

  RunLoopUntil([&play_sound_completed]() { return play_sound_completed; });
  RunLoopUntil([this]() { return renderers_completed(); });
  under_test_ptr()->RemoveSound(0);
  RunLoopUntilIdle();
}

// Plays a sound from a wav file. The audio renderer closes the connection on |AddPayloadBuffer|.
TEST_F(SoundPlayerTests, WavFileCloseConnection) {
  SetRendererExpectations({{
      .payload_buffer_ = ZX_KOID_INVALID,
      .packets_ = {{.pts = fuchsia::media::NO_TIMESTAMP,
                    .payload_buffer_id = 0,
                    .payload_offset = 0,
                    .payload_size = kWavFilePayloadSize,
                    .flags = 0,
                    .buffer_config = 0,
                    .stream_segment_id = 0}},
      .stream_type_ =
          {
              .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
              .channels = kWavFileChannels,
              .frames_per_second = kWavFramesPerSecond,
          },
      .usage_ = kUsage,
      .close_on_add_payload_buffer_ = true,
  }});

  under_test().AddSoundFromFile(0, ResourceFile("sfx.wav"),
                                [](fuchsia::media::sounds::Player_AddSoundFromFile_Result result) {
                                  EXPECT_TRUE(result.is_response());
                                  EXPECT_EQ(kWavFileDuration, result.response().duration);
                                });
  bool play_sound_completed = false;
  under_test().PlaySound(
      0, kUsage, [&play_sound_completed](fuchsia::media::sounds::Player_PlaySound_Result result) {
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(fuchsia::media::sounds::PlaySoundError::RENDERER_FAILED, result.err());
        play_sound_completed = true;
      });
  RunLoopUntil([&play_sound_completed]() { return play_sound_completed; });

  under_test().RemoveSound(0);
  RunLoopUntilIdle();
}

}  // namespace test
}  // namespace soundplayer
