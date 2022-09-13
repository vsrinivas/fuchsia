// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <stdio.h>

#include <memory>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/media/codec/codecs/test/test_codec_packets.h"
#include "src/media/codec/codecs/vaapi/codec_adapter_vaapi_decoder.h"
#include "src/media/codec/codecs/vaapi/codec_runner_app.h"
#include "src/media/codec/codecs/vaapi/vaapi_utils.h"
#include "vaapi_stubs.h"

namespace {

constexpr uint32_t kBearVideoWidth = 320u;
constexpr uint32_t kBearVideoHeight = 192u;
constexpr uint32_t kBearUncompressedFrameBytes = kBearVideoWidth * kBearVideoHeight * 3 / 2;

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

    std::lock_guard<std::mutex> guard(lock_);
    fail_codec_count_++;
    cond_.notify_all();
  }

  void onCoreCodecFailStream(fuchsia::media::StreamError error) override {
    printf("Got onCoreCodecFailStream %d\n", static_cast<int>(error));
    fflush(stdout);

    std::lock_guard<std::mutex> guard(lock_);
    fail_stream_count_++;
    cond_.notify_all();
  }

  void onCoreCodecResetStreamAfterCurrentFrame() override {}

  void onCoreCodecMidStreamOutputConstraintsChange(bool output_re_config_required) override {
    {
      std::lock_guard lock(lock_);
      // Test a representative value.
      output_constraints_ = codec_adapter_->CoreCodecGetBufferCollectionConstraints(
          CodecPort::kOutputPort, fuchsia::media::StreamBufferConstraints(),
          fuchsia::media::StreamBufferPartialSettings());
      EXPECT_TRUE(output_constraints_.buffer_memory_constraints.cpu_domain_supported);
      EXPECT_EQ(kBearVideoWidth,
                output_constraints_.image_format_constraints[0].required_min_coded_width);
      output_constraints_set_ = true;
      cond_.notify_all();
    }
    if (reconfigure_in_constraints_change_) {
      ReconfigureBuffers();
    }
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
    EXPECT_EQ(
        kBearVideoWidth,
        output_format.format_details().domain().video().uncompressed().image_format.coded_width);

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

  void ReconfigureBuffers() {
    std::unique_lock<std::mutex> lock(lock_);
    EXPECT_TRUE(output_constraints_set_);
    // Wait for buffer initialization to complete to ensure all buffers are staged to be loaded.
    cond_.wait(lock, [&]() { return buffer_initialization_completed_; });

    // Set the codec output format to the linear format
    auto output_constraints = codec_adapter_->CoreCodecGetBufferCollectionConstraints(
        CodecPort::kOutputPort, fuchsia::media::StreamBufferConstraints(),
        fuchsia::media::StreamBufferPartialSettings());
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
    buffer_collection.settings.image_format_constraints =
        output_constraints.image_format_constraints.at(0);
    buffer_collection.settings.has_image_format_constraints = true;
    buffer_collection.buffer_count = output_constraints.min_buffer_count_for_camping;
    EXPECT_FALSE(
        buffer_collection.settings.image_format_constraints.pixel_format.has_format_modifier);
    codec_adapter_->CoreCodecSetBufferCollectionInfo(CodecPort::kOutputPort, buffer_collection);
    codec_adapter_->CoreCodecMidStreamOutputBufferReConfigFinish();
  }

  void set_reconfigure_in_constraints_change(bool reconfig) {
    reconfigure_in_constraints_change_ = reconfig;
  }

  void WaitForOutputConstraintsSet() {
    std::unique_lock<std::mutex> lock(lock_);
    cond_.wait(lock, [&]() { return output_constraints_set_; });
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
  bool reconfigure_in_constraints_change_ = true;
  fuchsia::sysmem::BufferCollectionConstraints output_constraints_;
  bool output_constraints_set_ = false;
};

class H264VaapiTestFixture : public ::testing::Test {
 protected:
  H264VaapiTestFixture() = default;
  ~H264VaapiTestFixture() override { decoder_.reset(); }

  void SetUp() override {
    EXPECT_TRUE(VADisplayWrapper::InitializeSingletonForTesting());

    vaDefaultStubSetReturn();

    // Have to defer the construction of decoder_ until
    // VADisplayWrapper::InitializeSingletonForTesting is called
    decoder_ = std::make_unique<CodecAdapterVaApiDecoder>(lock_, &events_);
    events_.set_codec_adapter(decoder_.get());
  }

  void TearDown() override { vaDefaultStubSetReturn(); }

  void CodecAndStreamInit() {
    fuchsia::media::FormatDetails format_details;
    format_details.set_format_details_version_ordinal(1);
    format_details.set_mime_type("video/h264");
    decoder_->CoreCodecInit(format_details);

    auto input_constraints = decoder_->CoreCodecGetBufferCollectionConstraints(
        CodecPort::kInputPort, fuchsia::media::StreamBufferConstraints(),
        fuchsia::media::StreamBufferPartialSettings());
    EXPECT_TRUE(input_constraints.buffer_memory_constraints.cpu_domain_supported);

    decoder_->CoreCodecStartStream();
    decoder_->CoreCodecQueueInputFormatDetails(format_details);
  }

  void CodecStreamStop() {
    decoder_->CoreCodecStopStream();
    decoder_->CoreCodecEnsureBuffersNotConfigured(CodecPort::kOutputPort);
  }

  void ParseFileIntoInputPackets(const std::string &file_name) {
    std::vector<uint8_t> result;
    ASSERT_TRUE(files::ReadFileToVector(file_name, &result));

    input_buffer_ = std::make_unique<CodecBufferForTest>(result.size(), 0, false);
    std::memcpy(input_buffer_->base(), result.data(), result.size());

    input_packet_ = std::make_unique<CodecPacketForTest>(0);
    input_packet_->SetStartOffset(0);
    input_packet_->SetValidLengthBytes(static_cast<uint32_t>(result.size()));
    input_packet_->SetBuffer(input_buffer_.get());
    decoder_->CoreCodecQueueInputPacket(input_packet_.get());
  }

  void ConfigureOutputBuffers(uint32_t output_packet_count, size_t output_packet_size) {
    auto test_packets = Packets(output_packet_count);
    test_buffers_ = Buffers(std::vector<size_t>(output_packet_count, output_packet_size));

    test_packets_ = std::vector<std::unique_ptr<CodecPacket>>(output_packet_count);
    for (size_t i = 0; i < output_packet_count; i++) {
      auto &packet = test_packets.packets[i];
      test_packets_[i] = std::move(packet);
      decoder_->CoreCodecAddBuffer(CodecPort::kOutputPort, test_buffers_.buffers[i].get());
    }

    decoder_->CoreCodecConfigureBuffers(CodecPort::kOutputPort, test_packets_);
    for (size_t i = 0; i < output_packet_count; i++) {
      decoder_->CoreCodecRecycleOutputPacket(test_packets_[i].get());
    }

    decoder_->CoreCodecConfigureBuffers(CodecPort::kOutputPort, test_packets_);
  }

  std::mutex lock_;
  FakeCodecAdapterEvents events_;
  std::unique_ptr<CodecAdapterVaApiDecoder> decoder_;
  std::unique_ptr<CodecPacketForTest> input_packet_;
  std::unique_ptr<CodecBufferForTest> input_buffer_;
  TestBuffers test_buffers_;
  std::vector<std::unique_ptr<CodecPacket>> test_packets_;
};

TEST_F(H264VaapiTestFixture, MimeTypeMismatchFailure) {
  constexpr uint64_t kExpectedNumOfCodecFailures = 1u;

  fuchsia::media::FormatDetails format_details;
  format_details.set_format_details_version_ordinal(1);
  format_details.set_mime_type("video/h264");
  decoder_->CoreCodecInit(format_details);
  decoder_->CoreCodecStartStream();

  fuchsia::media::FormatDetails format_details_mismatch;
  format_details_mismatch.set_format_details_version_ordinal(1);
  format_details_mismatch.set_mime_type("video/vp9");
  decoder_->CoreCodecQueueInputFormatDetails(format_details_mismatch);

  events_.SetBufferInitializationCompleted();
  events_.WaitForCodecFailure(kExpectedNumOfCodecFailures);

  CodecStreamStop();

  EXPECT_EQ(kExpectedNumOfCodecFailures, events_.fail_codec_count());
  EXPECT_EQ(0u, events_.fail_stream_count());
}

TEST_F(H264VaapiTestFixture, DecodeBasic) {
  constexpr uint32_t kExpectedOutputPackets = 29;

  CodecAndStreamInit();

  // Should be enough to handle a large fraction of bear.h264 output without recycling.
  constexpr uint32_t kOutputPacketCount = 35;
  constexpr size_t kOutputPacketSize = kBearUncompressedFrameBytes;

  ParseFileIntoInputPackets("/pkg/data/bear.h264");
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

// Check that delaying the output buffer configuration for a while doesn't cause a crash when
// outputting frames.
TEST_F(H264VaapiTestFixture, DelayedConfiguration) {
  constexpr uint32_t kExpectedOutputPackets = 29;

  events_.set_reconfigure_in_constraints_change(false);

  CodecAndStreamInit();

  // Should be enough to handle a large fraction of bear.h264 output without recycling.
  constexpr uint32_t kOutputPacketCount = 35;
  constexpr size_t kOutputPacketSize = kBearUncompressedFrameBytes;

  ParseFileIntoInputPackets("/pkg/data/bear.h264");

  sleep(1);

  events_.WaitForOutputConstraintsSet();
  ConfigureOutputBuffers(kOutputPacketCount, kOutputPacketSize);
  events_.SetBufferInitializationCompleted();
  events_.ReconfigureBuffers();
  events_.WaitForInputPacketsDone();
  events_.WaitForOutputPacketCount(kExpectedOutputPackets);
  events_.ReturnLastOutputPacket();

  CodecStreamStop();

  // One packet was returned, so it was already removed from the list.
  EXPECT_EQ(kExpectedOutputPackets - 1u, events_.output_packet_count());

  EXPECT_EQ(0u, events_.fail_codec_count());
  EXPECT_EQ(0u, events_.fail_stream_count());
}

TEST(H264Vaapi, CodecList) {
  EXPECT_TRUE(VADisplayWrapper::InitializeSingletonForTesting());
  auto codec_list = GetCodecList();
  // video/h264 decode, video/h264-multi decode, video/vp9 decode, video/h264 encode
  EXPECT_EQ(4u, codec_list.size());
}

// Test that we can connect using the CodecFactory.
TEST(H264Vaapi, Init) {
  EXPECT_TRUE(VADisplayWrapper::InitializeSingletonForTesting());
  fidl::InterfaceRequest<fuchsia::io::Directory> directory_request;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto codec_services = sys::ServiceDirectory::CreateWithRequest(&directory_request);

  std::thread codec_thread([directory_request = std::move(directory_request)]() mutable {
    CodecRunnerApp<CodecAdapterVaApiDecoder, NoAdapter> runner_app;
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
  fuchsia::mediacodec::CreateDecoder_Params params;
  fuchsia::media::FormatDetails input_details;
  input_details.set_mime_type("video/h264");
  params.set_input_details(std::move(input_details));
  params.set_require_hw(true);
  EXPECT_EQ(ZX_OK, codec_factory->CreateDecoder(std::move(params), stream_processor.NewRequest()));

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
