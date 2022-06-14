// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/media/vnext/lib/builder/audio_conversion_pipeline.h"

namespace fmlib {
namespace {

class FakeDecoder : public fuchsia::audio::Decoder {
 public:
  FakeDecoder(fuchsia::mediastreams::AudioFormat format,
              fuchsia::mediastreams::Compression compression,
              fidl::InterfaceRequest<fuchsia::audio::Decoder> request)
      : format_(std::move(format)), binding_(this, std::move(request)) {
    binding_.set_error_handler([this](zx_status_t status) { delete this; });
  }

  ~FakeDecoder() override = default;

  // fuchsia::audio::Decoder implementation.
  void ConnectInputStream(zx::eventpair buffer_collection_token,
                          fuchsia::media2::PacketTimestampUnitsPtr timestamp_units,
                          fidl::InterfaceRequest<fuchsia::media2::StreamSink> stream_sink_request,
                          ConnectInputStreamCallback callback) override {
    input_stream_sink_request_ = std::move(stream_sink_request);
    callback(fpromise::ok());

    binding_.events().OnNewOutputStreamAvailable(fidl::Clone(format_), std::move(timestamp_units));
  }

  void ConnectOutputStream(zx::eventpair buffer_collection_token,
                           fuchsia::media2::StreamSinkHandle stream_sink,
                           ConnectOutputStreamCallback callback) override {
    output_stream_sink_handle_ = std::move(stream_sink);
    callback(fpromise::ok());
  }

  void DisconnectOutputStream() override { output_stream_sink_handle_ = nullptr; }

 private:
  fuchsia::mediastreams::AudioFormat format_;
  fidl::Binding<fuchsia::audio::Decoder> binding_;
  fidl::InterfaceRequest<fuchsia::media2::StreamSink> input_stream_sink_request_;
  fuchsia::media2::StreamSinkHandle output_stream_sink_handle_;
};

class FakeDecoderCreator : public fuchsia::audio::DecoderCreator {
 public:
  class Binder : public ServiceBinder {
   public:
    Binder() = default;

    ~Binder() override = default;

    void Bind(zx::channel channel) override { new FakeDecoderCreator(std::move(channel)); }
  };

  FakeDecoderCreator(zx::channel channel) : binding_(this, std::move(channel)) {
    binding_.set_error_handler([this](zx_status_t status) {
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, status);
      delete this;
    });
  }

  ~FakeDecoderCreator() override = default;

  // fuchsia::audio::DecoderCreator implementation.
  void Create(fuchsia::mediastreams::AudioFormat format,
              fuchsia::mediastreams::Compression compression,
              fidl::InterfaceRequest<fuchsia::audio::Decoder> request) override {
    new FakeDecoder(std::move(format), std::move(compression), std::move(request));
  }

 private:
  fidl::Binding<fuchsia::audio::DecoderCreator> binding_;
};

class AudioConversionPipelineTest : public gtest::RealLoopFixture {
 protected:
  AudioConversionPipelineTest()
      : thread_(Thread::CreateForLoop(loop())), service_provider_(thread_) {
    service_provider_.RegisterService(fuchsia::audio::DecoderCreator::Name_,
                                      std::make_unique<FakeDecoderCreator::Binder>());
  }

  Thread& thread() { return thread_; }

  ServiceProvider& service_provider() { return service_provider_; }

 private:
  Thread thread_;
  ServiceProvider service_provider_;
};

const fuchsia::mediastreams::AudioFormat kFormat{
    .sample_format = fuchsia::mediastreams::AudioSampleFormat::SIGNED_16,
    .channel_count = 2,
    .frames_per_second = 48000,
    .channel_layout = fuchsia::mediastreams::AudioChannelLayout::WithPlaceholder(0)};
const std::unique_ptr<std::string> kOpusCompressionType =
    std::make_unique<std::string>(fuchsia::mediastreams::AUDIO_COMPRESSION_OPUS);
const std::unique_ptr<std::string> kMp3CompressionType =
    std::make_unique<std::string>(fuchsia::mediastreams::AUDIO_COMPRESSION_MP3);

// Tests that no pipeline is created for uncompressed->uncompressed (no conversion).
TEST_F(AudioConversionPipelineTest, UncompressedInOut) {
  EXPECT_FALSE(!!AudioConversionPipeline::Create(kFormat, /* no input compression */ nullptr,
                                                 /* no output compression type*/ nullptr,
                                                 service_provider()));
}

// Tests that no pipeline is created for compressed->compressed (no conversion).
TEST_F(AudioConversionPipelineTest, CompressedInOut) {
  auto compression = fuchsia::mediastreams::Compression::New();
  compression->type = *kOpusCompressionType;
  EXPECT_FALSE(!!AudioConversionPipeline::Create(kFormat, std::move(compression),
                                                 kOpusCompressionType, service_provider()));
}

// Tests that a pipeline is created for uncompressed->compressed (encode) but fails to connect.
TEST_F(AudioConversionPipelineTest, Encode) {
  auto under_test =
      AudioConversionPipeline::Create(kFormat, nullptr, kOpusCompressionType, service_provider());
  EXPECT_TRUE(!!under_test);
  zx::eventpair provider_token;
  zx::eventpair participant_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &provider_token, &participant_token));
  fuchsia::media2::StreamSinkPtr stream_sink_ptr;
  bool task_ran = false;
  thread().schedule_task(
      under_test
          ->ConnectInputStream(std::move(participant_token),
                               /* timestamp_units*/ nullptr, stream_sink_ptr.NewRequest())
          .then([&task_ran](fpromise::result<void, fuchsia::media2::ConnectionError>& result) {
            EXPECT_TRUE(result.is_error());
            EXPECT_EQ(fuchsia::media2::ConnectionError::NOT_SUPPORTED, result.error());
            task_ran = true;
          }));

  RunLoopUntilIdle();
  EXPECT_TRUE(task_ran);
}

// Tests that a pipeline is created for compressed->compressed (transcode) but fails to connect.
TEST_F(AudioConversionPipelineTest, Transcode) {
  auto compression = fuchsia::mediastreams::Compression::New();
  compression->type = *kMp3CompressionType;
  auto under_test = AudioConversionPipeline::Create(kFormat, std::move(compression),
                                                    kOpusCompressionType, service_provider());
  EXPECT_TRUE(!!under_test);
  zx::eventpair provider_token;
  zx::eventpair participant_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &provider_token, &participant_token));
  fuchsia::media2::StreamSinkPtr stream_sink_ptr;
  bool task_ran = false;
  thread().schedule_task(
      under_test
          ->ConnectInputStream(std::move(participant_token),
                               /* timestamp_units*/ nullptr, stream_sink_ptr.NewRequest())
          .then([&task_ran](fpromise::result<void, fuchsia::media2::ConnectionError>& result) {
            EXPECT_TRUE(result.is_error());
            EXPECT_EQ(fuchsia::media2::ConnectionError::NOT_SUPPORTED, result.error());
            task_ran = true;
          }));

  RunLoopUntilIdle();
  EXPECT_TRUE(task_ran);
}

// Tests that a pipeline is created for compressed->uncompressed (decode) and successfully connects.
TEST_F(AudioConversionPipelineTest, Decode) {
  auto compression = fuchsia::mediastreams::Compression::New();
  compression->type = *kOpusCompressionType;
  auto under_test =
      AudioConversionPipeline::Create(kFormat, std::move(compression), nullptr, service_provider());
  EXPECT_TRUE(!!under_test);
  zx::eventpair provider_token;
  zx::eventpair participant_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &provider_token, &participant_token));
  fuchsia::media2::StreamSinkPtr stream_sink_ptr;
  bool task_ran = false;
  thread().schedule_task(
      under_test
          ->ConnectInputStream(std::move(participant_token),
                               /* timestamp_units*/ nullptr, stream_sink_ptr.NewRequest())
          .then([&task_ran](fpromise::result<void, fuchsia::media2::ConnectionError>& result) {
            EXPECT_TRUE(result.is_ok());
            task_ran = true;
          }));

  RunLoopUntilIdle();
  EXPECT_TRUE(task_ran);
  task_ran = false;

  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &provider_token, &participant_token));
  thread().schedule_task(
      under_test->ConnectOutputStream(std::move(participant_token), stream_sink_ptr.Unbind())
          .then([&task_ran](fpromise::result<void, fuchsia::media2::ConnectionError>& result) {
            EXPECT_TRUE(result.is_ok());
            task_ran = true;
          }));

  RunLoopUntilIdle();
  EXPECT_TRUE(task_ran);
}

}  // namespace
}  // namespace fmlib
