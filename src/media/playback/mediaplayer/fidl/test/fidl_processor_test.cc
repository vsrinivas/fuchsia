// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/fidl/fidl_processor.h"

#include "lib/fidl/cpp/binding.h"
#include "lib/gtest/real_loop_fixture.h"
#include "src/media/playback/mediaplayer/graph/types/audio_stream_type.h"

namespace media_player {
namespace test {

class FidlProcessorTest : public ::gtest::RealLoopFixture {
 protected:
};

// Fake implementation of |ServiceProvider|.
class FakeServiceProvider : public ServiceProvider {
 public:
  void ConnectToService(std::string service_path, zx::channel channel) override {}
};

// Fake implementation of |fuchsia::media::StreamProcessor|.
class FakeStreamProcessor : public fuchsia::media::StreamProcessor {
 public:
  FakeStreamProcessor() : binding_(this) {}

  void Bind(fidl::InterfaceRequest<fuchsia::media::StreamProcessor> request) {
    binding_.Bind(std::move(request));
  }

  // fuchsia::media::StreamProcessor implementation.
  void EnableOnStreamFailed() override {}
  void SetInputBufferPartialSettings(
      fuchsia::media::StreamBufferPartialSettings input_settings) override {}
  void SetOutputBufferPartialSettings(
      fuchsia::media::StreamBufferPartialSettings output_settings) override {}
  void CompleteOutputBufferPartialSettings(uint64_t buffer_lifetime_ordinal) override {}
  void FlushEndOfStreamAndCloseStream(uint64_t stream_lifetime_ordinal) override {}
  void CloseCurrentStream(uint64_t stream_lifetime_ordinal, bool release_input_buffers,
                          bool release_output_buffers) override {}
  void Sync(SyncCallback callback) override {}
  void RecycleOutputPacket(fuchsia::media::PacketHeader available_output_packet) override {}
  void QueueInputFormatDetails(uint64_t stream_lifetime_ordinal,
                               fuchsia::media::FormatDetails format_details) override {}
  void QueueInputPacket(fuchsia::media::Packet packet) override {}
  void QueueInputEndOfStream(uint64_t stream_lifetime_ordinal) override {}

 private:
  fidl::Binding<fuchsia::media::StreamProcessor> binding_;
};

// Tests that SetInputStreamType method produces the expected output stream type.
TEST_F(FidlProcessorTest, SetInputStreamType) {
  static constexpr AudioStreamType::SampleFormat kSampleFormat =
      AudioStreamType::SampleFormat::kSigned16;
  static constexpr uint32_t kChannels = 2;
  static constexpr uint32_t kFramesPerSecond = 48000;

  FakeServiceProvider service_provider;
  FakeStreamProcessor fake_processor;

  fuchsia::media::StreamProcessorPtr fake_processor_ptr;
  fidl::InterfaceRequest<fuchsia::media::StreamProcessor> processor_request =
      fake_processor_ptr.NewRequest();
  fake_processor.Bind(std::move(processor_request));

  std::shared_ptr<Processor> under_test =
      FidlProcessor::Create(&service_provider, StreamType::Medium::kAudio,
                            FidlProcessor::Function::kDecrypt, std::move(fake_processor_ptr));

  AudioStreamType input_stream_type(Bytes::Create(10),  // encryption_parameters
                                    StreamType::kAudioEncodingLpcm, nullptr, kSampleFormat,
                                    kChannels, kFramesPerSecond);
  under_test->SetInputStreamType(input_stream_type);

  auto output_stream_type = under_test->output_stream_type();
  EXPECT_EQ(StreamType::Medium::kAudio, output_stream_type->medium());
  EXPECT_EQ(StreamType::kAudioEncodingLpcm, output_stream_type->encoding());
  EXPECT_EQ(nullptr, output_stream_type->encoding_parameters());
  EXPECT_FALSE(output_stream_type->encrypted());
  auto audio_output_stream_type = output_stream_type->audio();
  EXPECT_TRUE(!!audio_output_stream_type);
  EXPECT_EQ(kSampleFormat, audio_output_stream_type->sample_format());
  EXPECT_EQ(kChannels, audio_output_stream_type->channels());
  EXPECT_EQ(kFramesPerSecond, audio_output_stream_type->frames_per_second());
}

}  // namespace test
}  // namespace media_player
