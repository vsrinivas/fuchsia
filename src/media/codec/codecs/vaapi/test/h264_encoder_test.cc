// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <stdio.h>

#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/media/codec/codecs/test/test_codec_packets.h"
#include "src/media/codec/codecs/vaapi/codec_adapter_vaapi_encoder.h"
#include "src/media/codec/codecs/vaapi/codec_runner_app.h"
#include "src/media/codec/codecs/vaapi/vaapi_utils.h"
#include "vaapi_stubs.h"

namespace {

class FakeCodecAdapterEvents : public CodecAdapterEvents {
 public:
  void onCoreCodecFailCodec(const char *format, ...) override {
    va_list args;
    va_start(args, format);
    printf("Got onCoreCodecFailCodec: ");
    vprintf(format, args);
    printf("\n");
    fflush(stdout);
    va_end(args);

    fail_codec_count_++;
    cond_.notify_all();
  }

  void onCoreCodecFailStream(fuchsia::media::StreamError error) override {
    printf("Got onCoreCodecFailStream %d\n", static_cast<int>(error));
    fflush(stdout);
    fail_stream_count_++;
  }

  void onCoreCodecResetStreamAfterCurrentFrame() override {}

  void onCoreCodecMidStreamOutputConstraintsChange(bool output_re_config_required) override {
    // Test a representative value.
    auto output_constraints = codec_adapter_->CoreCodecGetBufferCollectionConstraints(
        CodecPort::kOutputPort, fuchsia::media::StreamBufferConstraints(),
        fuchsia::media::StreamBufferPartialSettings());
    EXPECT_TRUE(output_constraints.buffer_memory_constraints.cpu_domain_supported);

    std::unique_lock<std::mutex> lock(lock_);
    // Wait for buffer initialization to complete to ensure all buffers are staged to be loaded.
    cond_.wait(lock, [&]() { return buffer_initialization_completed_; });

    // Fake out the client setting buffer constraints on sysmem
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
    buffer_collection.settings.image_format_constraints =
        output_constraints.image_format_constraints.at(0);
    buffer_collection.buffer_count = output_constraints.min_buffer_count_for_camping;
    codec_adapter_->CoreCodecSetBufferCollectionInfo(CodecPort::kOutputPort, buffer_collection);
    codec_adapter_->CoreCodecMidStreamOutputBufferReConfigFinish();
  }

  void onCoreCodecOutputFormatChange() override {}

  void onCoreCodecInputPacketDone(CodecPacket *packet) override {
    std::lock_guard lock(lock_);
    input_packets_done_.push_back(packet);
    cond_.notify_all();
  }

  void onCoreCodecOutputPacket(CodecPacket *packet, bool error_detected_before,
                               bool error_detected_during) override {
    auto output_format = codec_adapter_->CoreCodecGetOutputFormat(1u, 1u);
    // Test a representative value.
    EXPECT_TRUE(output_format.format_details().domain().video().is_compressed());

    std::lock_guard lock(lock_);
    output_packets_done_.push_back(packet);
    cond_.notify_all();
  }

  void onCoreCodecOutputEndOfStream(bool error_detected_before) override {
    printf("Got onCoreCodecOutputEndOfStream\n");
    fflush(stdout);
  }

  void onCoreCodecLogEvent(
      media_metrics::StreamProcessorEvents2MetricDimensionEvent event_code) override {}

  uint64_t fail_codec_count() const { return fail_codec_count_; }
  uint64_t fail_stream_count() const { return fail_stream_count_; }

  void WaitForInputPacketsDone() {
    std::unique_lock<std::mutex> lock(lock_);
    cond_.wait(lock, [this]() { return !input_packets_done_.empty(); });
  }

  void set_codec_adapter(CodecAdapter *codec_adapter) { codec_adapter_ = codec_adapter; }

  void WaitForOutputPacketCount(size_t output_packet_count) {
    std::unique_lock<std::mutex> lock(lock_);
    cond_.wait(lock, [&]() { return output_packets_done_.size() == output_packet_count; });
  }

  size_t output_packet_count() const { return output_packets_done_.size(); }

  void SetBufferInitializationCompleted() {
    std::lock_guard lock(lock_);
    buffer_initialization_completed_ = true;
    cond_.notify_all();
  }

  void WaitForCodecFailure(uint64_t failure_count) {
    std::unique_lock<std::mutex> lock(lock_);
    cond_.wait(lock, [&]() { return fail_codec_count_ == failure_count; });
  }

  void ReturnLastOutputPacket() {
    std::lock_guard lock(lock_);
    auto packet = output_packets_done_.back();
    output_packets_done_.pop_back();
    codec_adapter_->CoreCodecRecycleOutputPacket(packet);
  }

 private:
  CodecAdapter *codec_adapter_ = nullptr;
  uint64_t fail_codec_count_{};
  uint64_t fail_stream_count_{};

  std::mutex lock_;
  std::condition_variable cond_;

  std::vector<CodecPacket *> input_packets_done_;
  std::vector<CodecPacket *> output_packets_done_;
  bool buffer_initialization_completed_ = false;
};

class H264EncoderTestFixture : public ::testing::Test {
 protected:
  H264EncoderTestFixture() = default;
  ~H264EncoderTestFixture() override { encoder_.reset(); }

  void SetUp() override {
    EXPECT_TRUE(VADisplayWrapper::InitializeSingletonForTesting());

    vaDefaultStubSetReturn();

    // Have to defer the construction of encoder_ until
    // VADisplayWrapper::InitializeSingletonForTesting is called
    encoder_ = std::make_unique<CodecAdapterVaApiEncoder>(lock_, &events_);
    events_.set_codec_adapter(encoder_.get());
  }

  void TearDown() override { vaDefaultStubSetReturn(); }

  void CodecAndStreamInit() {
    fuchsia::media::FormatDetails format_details;
    format_details.set_format_details_version_ordinal(1);
    format_details.set_mime_type("video/h264");

    fuchsia::media::DomainFormat domain_format;
    domain_format.video().uncompressed().image_format.display_width = 10;
    domain_format.video().uncompressed().image_format.display_height = 10;
    domain_format.video().uncompressed().image_format.coded_width = 10;
    domain_format.video().uncompressed().image_format.coded_height = 10;
    format_details.set_domain(std::move(domain_format));
    encoder_->CoreCodecInit(format_details);

    auto input_constraints = encoder_->CoreCodecGetBufferCollectionConstraints(
        CodecPort::kInputPort, fuchsia::media::StreamBufferConstraints(),
        fuchsia::media::StreamBufferPartialSettings());
    EXPECT_TRUE(input_constraints.buffer_memory_constraints.cpu_domain_supported);

    encoder_->CoreCodecStartStream();
    encoder_->CoreCodecQueueInputFormatDetails(format_details);
  }

  void CodecStreamStop() {
    encoder_->CoreCodecStopStream();
    encoder_->CoreCodecEnsureBuffersNotConfigured(CodecPort::kOutputPort);
  }

  void ConfigureOutputBuffers(uint32_t output_packet_count, size_t output_packet_size) {
    auto test_packets = Packets(output_packet_count);
    test_buffers_ = Buffers(std::vector<size_t>(output_packet_count, output_packet_size));

    test_packets_ = std::vector<std::unique_ptr<CodecPacket>>(output_packet_count);
    for (size_t i = 0; i < output_packet_count; i++) {
      auto &packet = test_packets.packets[i];
      test_packets_[i] = std::move(packet);
      encoder_->CoreCodecAddBuffer(CodecPort::kOutputPort, test_buffers_.buffers[i].get());
    }

    encoder_->CoreCodecConfigureBuffers(CodecPort::kOutputPort, test_packets_);
    for (size_t i = 0; i < output_packet_count; i++) {
      encoder_->CoreCodecRecycleOutputPacket(test_packets_[i].get());
    }

    encoder_->CoreCodecConfigureBuffers(CodecPort::kOutputPort, test_packets_);
  }

  std::mutex lock_;
  FakeCodecAdapterEvents events_;
  std::unique_ptr<CodecAdapterVaApiEncoder> encoder_;
  std::unique_ptr<CodecPacketForTest> input_packet_;
  std::unique_ptr<CodecBufferForTest> input_buffer_;
  TestBuffers test_buffers_;
  std::vector<std::unique_ptr<CodecPacket>> test_packets_;
};

TEST_F(H264EncoderTestFixture, InvalidFormat) {
  constexpr uint64_t kExpectedNumOfCodecFailures = 1u;

  fuchsia::media::FormatDetails format_details;
  format_details.set_format_details_version_ordinal(1);
  format_details.set_mime_type("video/h264");
  encoder_->CoreCodecInit(format_details);
  events_.WaitForCodecFailure(kExpectedNumOfCodecFailures);

  EXPECT_EQ(kExpectedNumOfCodecFailures, events_.fail_codec_count());
  EXPECT_EQ(0u, events_.fail_stream_count());
}

TEST_F(H264EncoderTestFixture, Resize) {
  constexpr uint32_t kExpectedOutputPackets = 2;

  CodecAndStreamInit();

  // Should be enough to handle a large fraction of bear.h264 output without recycling.
  constexpr uint32_t kOutputPacketCount = 35;
  // Nothing writes to the output packet so its size doesn't matter.
  constexpr size_t kOutputPacketSize = 4096;
  {
    auto input_constraints = encoder_->CoreCodecGetBufferCollectionConstraints(
        CodecPort::kInputPort, fuchsia::media::StreamBufferConstraints(),
        fuchsia::media::StreamBufferPartialSettings());
    EXPECT_TRUE(input_constraints.buffer_memory_constraints.cpu_domain_supported);

    // Fake out the client setting buffer constraints on sysmem
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
    buffer_collection.settings.image_format_constraints =
        input_constraints.image_format_constraints.at(0);
    encoder_->CoreCodecSetBufferCollectionInfo(CodecPort::kInputPort, buffer_collection);
  }

  constexpr uint32_t kInputStride = 16;
  constexpr uint32_t kInputBufferSize = kInputStride * 12 * 3 / 2;

  input_buffer_ = std::make_unique<CodecBufferForTest>(kInputBufferSize, 0, false);

  std::vector<std::unique_ptr<CodecPacketForTest>> input_packets;
  {
    auto input_packet = std::make_unique<CodecPacketForTest>(0);
    input_packet->SetStartOffset(0);
    input_packet->SetValidLengthBytes(kInputBufferSize);
    input_packet->SetBuffer(input_buffer_.get());
    encoder_->CoreCodecQueueInputPacket(input_packet.get());
    input_packets.push_back(std::move(input_packet));
  }
  {
    fuchsia::media::FormatDetails format_details;
    format_details.set_format_details_version_ordinal(2);
    format_details.set_mime_type("video/h264");

    fuchsia::media::DomainFormat domain_format;
    domain_format.video().uncompressed().image_format.display_width = 12;
    domain_format.video().uncompressed().image_format.display_height = 10;
    domain_format.video().uncompressed().image_format.coded_width = 12;
    domain_format.video().uncompressed().image_format.coded_height = 10;
    format_details.set_domain(std::move(domain_format));
    encoder_->CoreCodecQueueInputFormatDetails(format_details);
  }
  {
    auto input_packet = std::make_unique<CodecPacketForTest>(0);
    input_packet->SetStartOffset(0);
    input_packet->SetValidLengthBytes(kInputBufferSize);
    input_packet->SetBuffer(input_buffer_.get());
    encoder_->CoreCodecQueueInputPacket(input_packet.get());
    input_packets.push_back(std::move(input_packet));
  }
  ConfigureOutputBuffers(kOutputPacketCount, kOutputPacketSize);

  events_.SetBufferInitializationCompleted();
  events_.WaitForInputPacketsDone();
  events_.WaitForOutputPacketCount(kExpectedOutputPackets);
  events_.ReturnLastOutputPacket();

  CodecStreamStop();

  // One packet was returned, so it was already removed from the list.
  EXPECT_EQ(kExpectedOutputPackets - 1u, events_.output_packet_count());

  EXPECT_EQ(0u, events_.fail_codec_count());
  EXPECT_EQ(0u, events_.fail_stream_count());
}

TEST_F(H264EncoderTestFixture, EncodeBasic) {
  constexpr uint32_t kExpectedOutputPackets = 29;

  CodecAndStreamInit();

  // Should be enough to handle a large fraction of bear.h264 output without recycling.
  constexpr uint32_t kOutputPacketCount = 35;
  // Nothing writes to the output packet so its size doesn't matter.
  constexpr size_t kOutputPacketSize = 4096;
  {
    auto input_constraints = encoder_->CoreCodecGetBufferCollectionConstraints(
        CodecPort::kInputPort, fuchsia::media::StreamBufferConstraints(),
        fuchsia::media::StreamBufferPartialSettings());
    EXPECT_TRUE(input_constraints.buffer_memory_constraints.cpu_domain_supported);

    // Fake out the client setting buffer constraints on sysmem
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
    buffer_collection.settings.image_format_constraints =
        input_constraints.image_format_constraints.at(0);
    encoder_->CoreCodecSetBufferCollectionInfo(CodecPort::kInputPort, buffer_collection);
  }

  constexpr uint32_t kInputStride = 16;
  constexpr uint32_t kInputBufferSize = kInputStride * 10 * 3 / 2;

  input_buffer_ = std::make_unique<CodecBufferForTest>(kInputBufferSize, 0, false);

  std::vector<std::unique_ptr<CodecPacketForTest>> input_packets;
  for (size_t i = 0; i < 29; i++) {
    auto input_packet = std::make_unique<CodecPacketForTest>(0);
    input_packet->SetStartOffset(0);
    input_packet->SetValidLengthBytes(kInputBufferSize);
    input_packet->SetBuffer(input_buffer_.get());
    encoder_->CoreCodecQueueInputPacket(input_packet.get());
    input_packets.push_back(std::move(input_packet));
  }
  ConfigureOutputBuffers(kOutputPacketCount, kOutputPacketSize);

  events_.SetBufferInitializationCompleted();
  events_.WaitForInputPacketsDone();
  events_.WaitForOutputPacketCount(kExpectedOutputPackets);
  events_.ReturnLastOutputPacket();

  CodecStreamStop();

  // One packet was returned, so it was already removed from the list.
  EXPECT_EQ(kExpectedOutputPackets - 1u, events_.output_packet_count());

  EXPECT_EQ(0u, events_.fail_codec_count());
  EXPECT_EQ(0u, events_.fail_stream_count());
}

// Test that we can connect using the CodecFactory.
TEST(H264Encoder, Init) {
  EXPECT_TRUE(VADisplayWrapper::InitializeSingletonForTesting());
  fidl::InterfaceRequest<fuchsia::io::Directory> directory_request;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto codec_services = sys::ServiceDirectory::CreateWithRequest(&directory_request);

  std::thread codec_thread([directory_request = std::move(directory_request)]() mutable {
    CodecRunnerApp<NoAdapter, CodecAdapterVaApiEncoder> runner_app;
    runner_app.Init();
    fidl::InterfaceHandle<fuchsia::io::Directory> outgoing_directory;
    EXPECT_EQ(ZX_OK,
              runner_app.component_context()->outgoing()->Serve(outgoing_directory.NewRequest()));
    EXPECT_EQ(ZX_OK, fdio_service_connect_at(outgoing_directory.channel().get(), "svc",
                                             directory_request.TakeChannel().release()));
    runner_app.Run();
  });

  fuchsia::mediacodec::CodecFactorySyncPtr codec_factory;
  codec_services->Connect(codec_factory.NewRequest());
  fuchsia::media::StreamProcessorPtr stream_processor;
  fuchsia::mediacodec::CreateEncoder_Params params;
  fuchsia::media::FormatDetails input_details;
  input_details.set_mime_type("video/h264");
  input_details.set_format_details_version_ordinal(1);

  fuchsia::media::DomainFormat domain_format;
  domain_format.video().uncompressed().image_format.display_width = 10;
  domain_format.video().uncompressed().image_format.display_height = 10;
  input_details.set_domain(std::move(domain_format));
  params.set_input_details(std::move(input_details));
  params.set_require_hw(true);
  EXPECT_EQ(ZX_OK, codec_factory->CreateEncoder(std::move(params), stream_processor.NewRequest()));

  stream_processor.set_error_handler([&](zx_status_t status) {
    loop.Quit();
    EXPECT_TRUE(false);
  });

  stream_processor.events().OnInputConstraints =
      [&](fuchsia::media::StreamBufferConstraints constraints) {
        loop.Quit();
        stream_processor.Unbind();
      };

  loop.Run();
  codec_factory.Unbind();

  codec_thread.join();
}

}  // namespace
