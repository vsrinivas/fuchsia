// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/media/vnext/lib/builder/video_conversion_pipeline.h"

namespace fmlib {
namespace {

class FakeDecoder : public fuchsia::video::Decoder {
 public:
  FakeDecoder(fuchsia::mediastreams::VideoFormat format,
              fuchsia::mediastreams::Compression compression,
              fidl::InterfaceRequest<fuchsia::video::Decoder> request)
      : format_(std::move(format)), binding_(this, std::move(request)) {
    binding_.set_error_handler([this](zx_status_t status) { delete this; });
  }

  ~FakeDecoder() override = default;

  // fuchsia::video::Decoder implementation.
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
  fuchsia::mediastreams::VideoFormat format_;
  fidl::Binding<fuchsia::video::Decoder> binding_;
  fidl::InterfaceRequest<fuchsia::media2::StreamSink> input_stream_sink_request_;
  fuchsia::media2::StreamSinkHandle output_stream_sink_handle_;
};

class FakeDecoderCreator : public fuchsia::video::DecoderCreator {
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

  // fuchsia::video::DecoderCreator implementation.
  void Create(fuchsia::mediastreams::VideoFormat format,
              fuchsia::mediastreams::Compression compression,
              fidl::InterfaceRequest<fuchsia::video::Decoder> request) override {
    new FakeDecoder(std::move(format), std::move(compression), std::move(request));
  }

 private:
  fidl::Binding<fuchsia::video::DecoderCreator> binding_;
};

class VideoConversionPipelineTest : public gtest::RealLoopFixture {
 protected:
  VideoConversionPipelineTest()
      : thread_(Thread::CreateForLoop(loop())), service_provider_(thread_) {
    service_provider_.RegisterService(fuchsia::video::DecoderCreator::Name_,
                                      std::make_unique<FakeDecoderCreator::Binder>());
  }

  Thread& thread() { return thread_; }

  ServiceProvider& service_provider() { return service_provider_; }

 private:
  Thread thread_;
  ServiceProvider service_provider_;
};

const fuchsia::mediastreams::VideoFormat kFormat{
    .pixel_format = fuchsia::mediastreams::PixelFormat::NV12,
    .pixel_format_modifier = 0,
    .color_space = fuchsia::mediastreams::ColorSpace::REC709,
    .coded_size = fuchsia::math::Size{.width = 640, .height = 480},
    .display_size = fuchsia::math::Size{.width = 640, .height = 480},
    .aspect_ratio = nullptr};
const std::unique_ptr<std::string> kH264CompressionType =
    std::make_unique<std::string>(fuchsia::mediastreams::VIDEO_COMPRESSION_H264);
const std::unique_ptr<std::string> kTheoraCompressionType =
    std::make_unique<std::string>(fuchsia::mediastreams::VIDEO_COMPRESSION_THEORA);

// Tests that no pipeline is created for uncompressed->uncompressed (no conversion).
TEST_F(VideoConversionPipelineTest, UncompressedInOut) {
  EXPECT_FALSE(!!VideoConversionPipeline::Create(kFormat, /* no input compression */ nullptr,
                                                 /* no output compression type*/ nullptr,
                                                 service_provider()));
}

// Tests that no pipeline is created for compressed->compressed (no conversion).
TEST_F(VideoConversionPipelineTest, CompressedInOut) {
  auto compression = fuchsia::mediastreams::Compression::New();
  compression->type = *kH264CompressionType;
  EXPECT_FALSE(!!VideoConversionPipeline::Create(kFormat, std::move(compression),
                                                 kH264CompressionType, service_provider()));
}

// Tests that a pipeline is created for uncompressed->compressed (encode) but fails to connect.
TEST_F(VideoConversionPipelineTest, Encode) {
  auto under_test =
      VideoConversionPipeline::Create(kFormat, nullptr, kH264CompressionType, service_provider());
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
TEST_F(VideoConversionPipelineTest, Transcode) {
  auto compression = fuchsia::mediastreams::Compression::New();
  compression->type = *kTheoraCompressionType;
  auto under_test = VideoConversionPipeline::Create(kFormat, std::move(compression),
                                                    kH264CompressionType, service_provider());
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
TEST_F(VideoConversionPipelineTest, Decode) {
  auto compression = fuchsia::mediastreams::Compression::New();
  compression->type = *kH264CompressionType;
  auto under_test =
      VideoConversionPipeline::Create(kFormat, std::move(compression), nullptr, service_provider());
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
