// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <stdio.h>

#include <thread>

#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/media/codec/codecs/test/test_codec_packets.h"
#include "src/media/codec/codecs/vaapi/codec_adapter_vaapi_decoder.h"
#include "src/media/codec/codecs/vaapi/codec_runner_app.h"
#include "src/media/codec/codecs/vaapi/vaapi_utils.h"

static int global_display_ptr;

VADisplay vaGetDisplayMagma(magma_device_t device) { return &global_display_ptr; }

namespace {

constexpr uint32_t kBearVideoWidth = 320u;

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
    EXPECT_EQ(kBearVideoWidth,
              output_constraints.image_format_constraints[0].required_min_coded_width);

    std::unique_lock<std::mutex> lock(lock_);
    // Wait for buffer initialization to complete to ensure all buffers are staged to be loaded.
    cond_.wait(lock, [&]() { return buffer_initialization_completed_; });
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

TEST(H264Vaapi, DecodeBasic) {
  std::mutex lock;

  EXPECT_TRUE(VADisplayWrapper::InitializeSingletonForTesting());

  constexpr uint32_t kExpectedOutputPackets = 29;

  FakeCodecAdapterEvents events;
  {
    CodecAdapterVaApiDecoder decoder(lock, &events);
    events.set_codec_adapter(&decoder);
    fuchsia::media::FormatDetails format_details;
    format_details.set_format_details_version_ordinal(1);
    format_details.set_mime_type("video/h264");
    decoder.CoreCodecInit(format_details);

    auto input_constraints = decoder.CoreCodecGetBufferCollectionConstraints(
        CodecPort::kInputPort, fuchsia::media::StreamBufferConstraints(),
        fuchsia::media::StreamBufferPartialSettings());
    EXPECT_TRUE(input_constraints.buffer_memory_constraints.cpu_domain_supported);

    decoder.CoreCodecStartStream();
    decoder.CoreCodecQueueInputFormatDetails(format_details);

    std::vector<uint8_t> result;
    ASSERT_TRUE(files::ReadFileToVector("/pkg/data/bear.h264", &result));

    CodecBufferForTest buffer_for_test(result.size(), 0, false);
    memcpy(buffer_for_test.base(), result.data(), result.size());

    CodecPacketForTest packet(0);
    packet.SetStartOffset(0);
    packet.SetValidLengthBytes(static_cast<uint32_t>(result.size()));
    packet.SetBuffer(&buffer_for_test);
    decoder.CoreCodecQueueInputPacket(&packet);

    // Should be enough to handle a large fraction of bear.h264 output without recycling.
    constexpr uint32_t kOutputPacketCount = 35;
    // Nothing writes to the output packet so its size doesn't matter.
    constexpr size_t kOutputPacketSize = 4096;
    auto test_packets = Packets(kOutputPacketCount);
    auto test_buffers = Buffers(std::vector<size_t>(kOutputPacketCount, kOutputPacketSize));

    std::vector<std::unique_ptr<CodecPacket>> packets(kOutputPacketCount);
    for (size_t i = 0; i < kOutputPacketCount; i++) {
      auto &packet = test_packets.packets[i];
      packets[i] = std::move(packet);
      decoder.CoreCodecAddBuffer(CodecPort::kOutputPort, test_buffers.buffers[i].get());
    }

    decoder.CoreCodecConfigureBuffers(CodecPort::kOutputPort, packets);
    for (size_t i = 0; i < kOutputPacketCount; i++) {
      decoder.CoreCodecRecycleOutputPacket(packets[i].get());
    }

    decoder.CoreCodecConfigureBuffers(CodecPort::kOutputPort, packets);
    events.SetBufferInitializationCompleted();
    events.WaitForInputPacketsDone();
    events.WaitForOutputPacketCount(kExpectedOutputPackets);
    events.ReturnLastOutputPacket();
    decoder.CoreCodecStopStream();
    decoder.CoreCodecEnsureBuffersNotConfigured(CodecPort::kOutputPort);
  }

  // One packet was returned, so it was already removed from the list.
  EXPECT_EQ(kExpectedOutputPackets - 1u, events.output_packet_count());

  EXPECT_EQ(0u, events.fail_codec_count());
  EXPECT_EQ(0u, events.fail_stream_count());
}

TEST(H264Vaapi, CodecList) {
  EXPECT_TRUE(VADisplayWrapper::InitializeSingletonForTesting());
  auto codec_list = GetCodecList();
  EXPECT_EQ(3u, codec_list.size());
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
    EXPECT_EQ(ZX_OK, runner_app.component_context()->outgoing()->Serve(
                         outgoing_directory.NewRequest().TakeChannel()));
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
