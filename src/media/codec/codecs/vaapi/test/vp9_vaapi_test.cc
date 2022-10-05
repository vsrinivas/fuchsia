// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fit/result.h>
#include <lib/syslog/global.h>
#include <stdio.h>

#include <cstdint>
#include <cstring>
#include <limits>
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

namespace test {

constexpr char kIvfHeaderSignature[] = "DKIF";

struct __attribute__((packed)) IvfFileHeader {
  char signature[4];      // "DKIF"
  uint16_t version;       // always zero
  uint16_t header_size;   // length of header in bytes
  uint32_t fourcc;        // codec FourCC
  uint16_t width;         // width in pixels
  uint16_t height;        // height in pixels
  uint32_t timebase_dem;  // timebase denumerator that defines the unit of IvfFrameHeader.timestamp
                          // in seconds. If num = 2 and dem = 30 then the unit of
                          // IvfFrameHeader.timestamp is 2/30 seconds.
  uint32_t timebase_num;  // timebase numerator
  uint32_t num_frames;    // number of frames in file
  uint32_t unused;
};
static_assert(sizeof(IvfFileHeader) == 32, "IvfFileHeader is the incorrect size");

struct __attribute__((packed)) IvfFrameHeader {
  uint32_t frame_size;  // Size of frame in bytes (does not include header)
  uint64_t timestamp;   // timestamp in units defined in IvfFileHeader
};
static_assert(sizeof(IvfFrameHeader) == 12, "IvfFrameHeader is the incorrect size");

// IVF is a simple file container for VP9 streams. Since Fuchsia is little endian we can just do
// memcpy's and memcmp's not having to worry about byte swaps.
class IvfParser {
 public:
  IvfParser() = default;
  ~IvfParser() = default;

  IvfParser(const IvfParser&) = delete;
  IvfParser& operator=(const IvfParser&) = delete;
  IvfParser(IvfParser&&) = delete;
  IvfParser& operator=(IvfParser&&) = delete;

  fit::result<std::string, IvfFileHeader> ReadFileHeader(const uint8_t* stream, size_t size) {
    ptr_ = stream;
    end_ = stream + size;

    if (size < sizeof(IvfFileHeader)) {
      return fit::error("EOF before file header");
    }

    IvfFileHeader file_header;
    std::memcpy(&file_header, ptr_, sizeof(IvfFileHeader));

    if (std::memcmp(file_header.signature, kIvfHeaderSignature, sizeof(file_header.signature)) !=
        0) {
      return fit::error("IVF signature not valid");
    }

    if (file_header.version != 0) {
      return fit::error("IVF version unknown");
    }

    if (file_header.header_size != sizeof(IvfFileHeader)) {
      return fit::error("IVF invalid header file");
    }

    ptr_ += sizeof(IvfFileHeader);
    return fit::ok(std::move(file_header));
  }

  fit::result<std::string, std::pair<IvfFrameHeader, const uint8_t*>> ParseFrame() {
    if (static_cast<std::size_t>(end_ - ptr_) < sizeof(IvfFrameHeader)) {
      return fit::error("Not enough space to parse frame header");
    }

    IvfFrameHeader frame_header;
    std::memcpy(&frame_header, ptr_, sizeof(IvfFrameHeader));
    ptr_ += sizeof(IvfFrameHeader);

    if (static_cast<uint32_t>(end_ - ptr_) < frame_header.frame_size) {
      return fit::error("Not enough space to parse frame payload");
    }

    const uint8_t* payload = ptr_;
    ptr_ += frame_header.frame_size;

    return fit::ok(std::make_pair(frame_header, payload));
  }

 private:
  // Current reading position of input stream.
  const uint8_t* ptr_{nullptr};

  // The end position of input stream.
  const uint8_t* end_{nullptr};
};

constexpr uint32_t kVideoWidth = 320u;
constexpr uint32_t kVideoHeight = 240u;
constexpr uint32_t kVideoBytes = kVideoWidth * kVideoHeight * 3 / 2;

class FakeCodecAdapterEvents : public CodecAdapterEvents {
 public:
  FakeCodecAdapterEvents() { loop_.StartThread("stream_control"); }

  void onCoreCodecFailCodec(const char* format, ...) override {
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

  void onCoreCodecResetStreamAfterCurrentFrame() override {
    // This call must be called on the stream_control_thread
    async::PostTask(loop_.dispatcher(),
                    [this] { codec_adapter_->CoreCodecResetStreamAfterCurrentFrame(); });
  }

  void onCoreCodecMidStreamOutputConstraintsChange(bool output_re_config_required) override {
    // Test a representative value.
    auto output_constraints = codec_adapter_->CoreCodecGetBufferCollectionConstraints(
        CodecPort::kOutputPort, fuchsia::media::StreamBufferConstraints(),
        fuchsia::media::StreamBufferPartialSettings());
    EXPECT_TRUE(output_constraints.buffer_memory_constraints.cpu_domain_supported);
    EXPECT_EQ(kVideoWidth, output_constraints.image_format_constraints[0].required_min_coded_width);

    std::unique_lock<std::mutex> lock(lock_);
    // Wait for buffer initialization to complete to ensure all buffers are staged to be loaded.
    cond_.wait(lock, [&]() { return buffer_initialization_completed_; });

    // Set the codec output format to the linear format and other various fields that sysmem would
    // normally populate. This is not meant to be an implementation of sysmem, only what is needed
    // for the test to work.
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

  void onCoreCodecOutputFormatChange() override {}

  void onCoreCodecInputPacketDone(CodecPacket* packet) override {
    std::lock_guard lock(lock_);
    input_packets_done_.push_back(packet);
    cond_.notify_all();
  }

  void onCoreCodecOutputPacket(CodecPacket* packet, bool error_detected_before,
                               bool error_detected_during) override {
    auto output_format = codec_adapter_->CoreCodecGetOutputFormat(1u, 1u);

    const auto& image_format =
        output_format.format_details().domain().video().uncompressed().image_format;

    // Test a representative value.
    EXPECT_EQ(kVideoWidth, image_format.coded_width);
    EXPECT_EQ(kVideoHeight, image_format.coded_height);
    EXPECT_EQ(fuchsia::sysmem::PixelFormatType::NV12, image_format.pixel_format.type);
    EXPECT_EQ(fuchsia::sysmem::ColorSpaceType::REC709, image_format.color_space.type);

    std::lock_guard lock(lock_);
    output_packets_done_.push_back(packet);
    cond_.notify_all();
  }

  void onCoreCodecOutputEndOfStream(bool error_detected_before) override {}

  void onCoreCodecLogEvent(
      media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent event_code) override {}

  uint64_t fail_codec_count() const { return fail_codec_count_; }
  uint64_t fail_stream_count() const { return fail_stream_count_; }

  void WaitForInputPacketsDone() {
    std::unique_lock<std::mutex> lock(lock_);
    cond_.wait(lock, [this]() { return !input_packets_done_.empty(); });
  }

  void set_codec_adapter(CodecAdapter* codec_adapter) { codec_adapter_ = codec_adapter; }

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

 private:
  CodecAdapter* codec_adapter_ = nullptr;
  uint64_t fail_codec_count_{};
  uint64_t fail_stream_count_{};

  std::mutex lock_;
  std::condition_variable cond_;

  std::vector<CodecPacket*> input_packets_done_;
  std::vector<CodecPacket*> output_packets_done_;
  bool buffer_initialization_completed_ = false;

  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
};

class Vp9VaapiTestFixture : public ::testing::Test {
 protected:
  Vp9VaapiTestFixture() = default;
  ~Vp9VaapiTestFixture() override { decoder_.reset(); }

  void SetUp() override {
    EXPECT_TRUE(VADisplayWrapper::InitializeSingletonForTesting());

    vaDefaultStubSetReturn();

    // Have to defer the construction of decoder_ until
    // VADisplayWrapper::InitializeSingletonForTesting is called
    decoder_ = std::make_unique<CodecAdapterVaApiDecoder>(lock_, &events_);
    events_.set_codec_adapter(decoder_.get());
  }

  void TearDown() override { vaDefaultStubSetReturn(); }

  void BlockCodecAndStreamInit() {
    fuchsia::media::FormatDetails format_details;
    format_details.set_format_details_version_ordinal(1);
    format_details.set_mime_type("video/vp9");
    decoder_->CoreCodecInit(format_details);

    auto input_constraints = decoder_->CoreCodecGetBufferCollectionConstraints(
        CodecPort::kInputPort, fuchsia::media::StreamBufferConstraints(),
        fuchsia::media::StreamBufferPartialSettings());
    EXPECT_TRUE(input_constraints.buffer_memory_constraints.cpu_domain_supported);

    {
      std::lock_guard<std::mutex> guard(lock_);
      block_input_processing_loop_ = true;
    }

    auto dispatcher = decoder_->input_processing_loop_.dispatcher();
    async::PostTask(dispatcher, [this] {
      std::unique_lock<std::mutex> lock(lock_);
      block_input_processing_loop_cv_.wait(lock, [this] { return !block_input_processing_loop_; });
    });

    decoder_->CoreCodecStartStream();
    decoder_->CoreCodecQueueInputFormatDetails(format_details);
  }

  void UnblockInputProcessingLoop() {
    std::lock_guard<std::mutex> guard(lock_);
    block_input_processing_loop_ = false;
    block_input_processing_loop_cv_.notify_all();
  }

  void CodecAndStreamInit() {
    fuchsia::media::FormatDetails format_details;
    format_details.set_format_details_version_ordinal(1);
    format_details.set_mime_type("video/vp9");
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

  fit::result<std::string, IvfFileHeader> InitializeIvfFile(const std::string& file_name) {
    if (!files::ReadFileToVector(file_name, &ivf_file_data_)) {
      return fit::error("Could not read file at " + file_name);
    }

    return ivf_parser_.ReadFileHeader(ivf_file_data_.data(), ivf_file_data_.size());
  }

  void ParseIvfFileIntoPackets(
      uint32_t num_of_packets_to_skip = 0u,
      uint32_t num_of_packets_to_parse = std::numeric_limits<uint32_t>::max()) {
    // While we have IVF frames create a new input packet to feed to the decoder. VP9 parser expects
    // the packets to be on VP9Frame boundaries and if not will parse multiple VP9 frames as one
    // frame. The packets will share the same underlying VMO buffer but will be offset in the
    // buffer.
    std::vector<uint8_t> payload;
    uint32_t packet_index = 0;

    while (packet_index < num_of_packets_to_skip) {
      auto parse_frame = ivf_parser_.ParseFrame();

      if (parse_frame.is_error()) {
        break;
      }

      packet_index += 1;
    }

    while (packet_index < num_of_packets_to_parse) {
      auto parse_frame = ivf_parser_.ParseFrame();

      if (parse_frame.is_error()) {
        break;
      }

      auto [frame_header, frame_payload] = parse_frame.value();
      const std::size_t current_size = payload.size();
      payload.resize(current_size + frame_header.frame_size);
      std::memcpy(&payload[current_size], frame_payload, frame_header.frame_size);

      auto input_packet =
          std::make_unique<CodecPacketForTest>(packet_index - num_of_packets_to_skip);
      input_packet->SetStartOffset(static_cast<uint32_t>(current_size));
      input_packet->SetValidLengthBytes(frame_header.frame_size);
      input_packets_.packets.push_back(std::move(input_packet));

      packet_index += 1;
    }

    // Create a VMO to hold all the VP9 data parsed from the IVF data file and copy the data into
    // the VMO
    test_buffer_ = std::make_unique<CodecBufferForTest>(payload.size(), 0, false);
    std::memcpy(test_buffer_->base(), payload.data(), payload.size());

    // Retroactively set the buffer for the packet and feed the decoder, in packet order. VP9
    // decoders do not support packet reordering
    for (auto& packet : input_packets_.packets) {
      packet->SetBuffer(test_buffer_.get());
      decoder_->CoreCodecQueueInputPacket(packet.get());
    }
  }

  void ConfigureOutputBuffers(uint32_t output_packet_count, size_t output_packet_size) {
    auto test_packets = Packets(output_packet_count);
    test_buffers_ = Buffers(std::vector<size_t>(output_packet_count, output_packet_size));

    test_packets_ = std::vector<std::unique_ptr<CodecPacket>>(output_packet_count);
    for (size_t i = 0; i < output_packet_count; i++) {
      auto& packet = test_packets.packets[i];
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
  std::vector<uint8_t> ivf_file_data_;
  std::unique_ptr<CodecAdapterVaApiDecoder> decoder_;
  IvfParser ivf_parser_;
  TestPackets input_packets_;
  std::unique_ptr<CodecBufferForTest> test_buffer_;
  TestBuffers test_buffers_;
  std::vector<std::unique_ptr<CodecPacket>> test_packets_;

  bool block_input_processing_loop_;
  std::condition_variable block_input_processing_loop_cv_;
};

TEST_F(Vp9VaapiTestFixture, NoFormatDetailsFailure) {
  constexpr uint64_t kExpectedNumOfCodecFailures = 1u;

  fuchsia::media::FormatDetails format_details;
  decoder_->CoreCodecInit(format_details);

  EXPECT_EQ(kExpectedNumOfCodecFailures, events_.fail_codec_count());
  EXPECT_EQ(0u, events_.fail_stream_count());
}

TEST_F(Vp9VaapiTestFixture, MimeTypeMismatchFailure) {
  constexpr uint64_t kExpectedNumOfCodecFailures = 1u;

  fuchsia::media::FormatDetails format_details;
  format_details.set_format_details_version_ordinal(1);
  format_details.set_mime_type("video/vp9");
  decoder_->CoreCodecInit(format_details);
  decoder_->CoreCodecStartStream();

  fuchsia::media::FormatDetails format_details_mismatch;
  format_details_mismatch.set_format_details_version_ordinal(1);
  format_details_mismatch.set_mime_type("video/h264");
  decoder_->CoreCodecQueueInputFormatDetails(format_details_mismatch);

  events_.SetBufferInitializationCompleted();
  events_.WaitForCodecFailure(kExpectedNumOfCodecFailures);

  CodecStreamStop();

  EXPECT_EQ(kExpectedNumOfCodecFailures, events_.fail_codec_count());
  EXPECT_EQ(0u, events_.fail_stream_count());
}

TEST_F(Vp9VaapiTestFixture, CreateConfigFailure) {
  constexpr uint64_t kExpectedNumOfCodecFailures = 1u;

  // Cause vaCreateConfig to return a failure
  vaCreateConfigStubSetReturn(VA_STATUS_ERROR_OPERATION_FAILED);

  fuchsia::media::FormatDetails format_details;
  format_details.set_format_details_version_ordinal(1);
  format_details.set_mime_type("video/vp9");
  decoder_->CoreCodecInit(format_details);

  events_.WaitForCodecFailure(kExpectedNumOfCodecFailures);

  EXPECT_EQ(kExpectedNumOfCodecFailures, events_.fail_codec_count());
  EXPECT_EQ(0u, events_.fail_stream_count());
}

TEST_F(Vp9VaapiTestFixture, CreateContextFailure) {
  constexpr uint64_t kExpectedNumOfCodecFailures = 1u;

  // Cause vaCreateContext to return a failure
  vaCreateContextStubSetReturn(VA_STATUS_ERROR_OPERATION_FAILED);

  CodecAndStreamInit();

  auto ivf_file_header_result = InitializeIvfFile("/pkg/data/test-25fps.vp9");

  if (ivf_file_header_result.is_error()) {
    FAIL() << ivf_file_header_result.error_value();
  }

  // Ensure the IVF header is what we are expecting
  auto ivf_file_header = ivf_file_header_result.value();
  EXPECT_EQ(0u, ivf_file_header.version);
  EXPECT_EQ(32u, ivf_file_header.header_size);
  EXPECT_EQ(0x30395056u, ivf_file_header.fourcc);  // VP90
  EXPECT_EQ(kVideoWidth, ivf_file_header.width);
  EXPECT_EQ(kVideoHeight, ivf_file_header.height);
  EXPECT_EQ(250u, ivf_file_header.num_frames);

  // Since each decoded frame will be its own output packet, create enough so we don't have to
  // recycle them.
  constexpr uint32_t kOutputPacketCount = 255u;

  constexpr size_t kOutputPacketSize = kVideoBytes;

  ParseIvfFileIntoPackets(0u, 1u);
  ConfigureOutputBuffers(kOutputPacketCount, kOutputPacketSize);

  events_.SetBufferInitializationCompleted();
  events_.WaitForInputPacketsDone();
  events_.WaitForCodecFailure(kExpectedNumOfCodecFailures);

  CodecStreamStop();

  EXPECT_EQ(kExpectedNumOfCodecFailures, events_.fail_codec_count());
  EXPECT_EQ(0u, events_.fail_stream_count());
}

TEST_F(Vp9VaapiTestFixture, CreateSurfacesFailure) {
  constexpr uint64_t kExpectedNumOfCodecFailures = 1u;

  // Cause vaCreateContext to return a failure
  vaCreateContextStubSetReturn(VA_STATUS_ERROR_OPERATION_FAILED);

  CodecAndStreamInit();

  auto ivf_file_header_result = InitializeIvfFile("/pkg/data/test-25fps.vp9");

  if (ivf_file_header_result.is_error()) {
    FAIL() << ivf_file_header_result.error_value();
  }

  // Ensure the IVF header is what we are expecting
  auto ivf_file_header = ivf_file_header_result.value();
  EXPECT_EQ(0u, ivf_file_header.version);
  EXPECT_EQ(32u, ivf_file_header.header_size);
  EXPECT_EQ(0x30395056u, ivf_file_header.fourcc);  // VP90
  EXPECT_EQ(kVideoWidth, ivf_file_header.width);
  EXPECT_EQ(kVideoHeight, ivf_file_header.height);
  EXPECT_EQ(250u, ivf_file_header.num_frames);

  // Since each decoded frame will be its own output packet, create enough so we don't have to
  // recycle them.
  constexpr uint32_t kOutputPacketCount = 255u;

  constexpr size_t kOutputPacketSize = kVideoBytes;

  ParseIvfFileIntoPackets(0u, 1u);
  ConfigureOutputBuffers(kOutputPacketCount, kOutputPacketSize);

  events_.SetBufferInitializationCompleted();
  events_.WaitForInputPacketsDone();
  events_.WaitForCodecFailure(kExpectedNumOfCodecFailures);

  CodecStreamStop();

  EXPECT_EQ(kExpectedNumOfCodecFailures, events_.fail_codec_count());
  EXPECT_EQ(0u, events_.fail_stream_count());
}

// We don't have a connection to system for the stub test, but verify that we can no longer select
// the tiled constraints after the output buffers are configured.
TEST_F(Vp9VaapiTestFixture, AttemptToSwitchFormatModifier) {
  constexpr uint64_t kExpectedNumOfCodecFailures = 0u;
  constexpr uint64_t kExpectedNumOfStreamFailures = 0u;
  constexpr uint32_t kExpectedOutputPackets = 1u;

  fuchsia::media::FormatDetails format_details;
  format_details.set_format_details_version_ordinal(1);
  format_details.set_mime_type("video/vp9");
  decoder_->CoreCodecInit(format_details);

  {
    auto pre_cfg_constraints = decoder_->CoreCodecGetBufferCollectionConstraints(
        CodecPort::kOutputPort, fuchsia::media::StreamBufferConstraints(),
        fuchsia::media::StreamBufferPartialSettings());

    ASSERT_EQ(pre_cfg_constraints.image_format_constraints_count, 2u);

    const auto& linear_pixel_format = pre_cfg_constraints.image_format_constraints[0u].pixel_format;
    EXPECT_TRUE(!linear_pixel_format.has_format_modifier ||
                linear_pixel_format.format_modifier.value ==
                    fuchsia::sysmem::FORMAT_MODIFIER_LINEAR);

    const auto& tiled_pixel_format = pre_cfg_constraints.image_format_constraints[1u].pixel_format;
    EXPECT_TRUE(tiled_pixel_format.has_format_modifier);
    EXPECT_TRUE(tiled_pixel_format.format_modifier.value ==
                fuchsia::sysmem::FORMAT_MODIFIER_INTEL_I915_Y_TILED);
  }

  decoder_->CoreCodecStartStream();
  decoder_->CoreCodecQueueInputFormatDetails(format_details);

  // Since each decoded frame will be its own output packet, create enough so we don't have to
  // recycle them.
  constexpr uint32_t kOutputPacketCount = 10u;
  constexpr size_t kOutputPacketSize = kVideoBytes;

  auto ivf_file_header_result = InitializeIvfFile("/pkg/data/test-25fps.vp9");
  if (ivf_file_header_result.is_error()) {
    FAIL() << ivf_file_header_result.error_value();
  }
  ParseIvfFileIntoPackets(0u, 1u);
  ConfigureOutputBuffers(kOutputPacketCount, kOutputPacketSize);

  events_.SetBufferInitializationCompleted();
  events_.WaitForInputPacketsDone();

  {
    auto post_cfg_constraints = decoder_->CoreCodecGetBufferCollectionConstraints(
        CodecPort::kOutputPort, fuchsia::media::StreamBufferConstraints(),
        fuchsia::media::StreamBufferPartialSettings());

    ASSERT_EQ(post_cfg_constraints.image_format_constraints_count, 1u);

    const auto& pixel_format = post_cfg_constraints.image_format_constraints[0u].pixel_format;
    EXPECT_TRUE(!pixel_format.has_format_modifier ||
                pixel_format.format_modifier.value == fuchsia::sysmem::FORMAT_MODIFIER_LINEAR);
  }

  EXPECT_EQ(kExpectedOutputPackets, events_.output_packet_count());
  CodecStreamStop();

  EXPECT_EQ(kExpectedNumOfCodecFailures, events_.fail_codec_count());
  EXPECT_EQ(kExpectedNumOfStreamFailures, events_.fail_stream_count());
}

TEST_F(Vp9VaapiTestFixture, DecodeBasic) {
  constexpr uint32_t kExpectedOutputPackets = 250u;

  CodecAndStreamInit();

  auto ivf_file_header_result = InitializeIvfFile("/pkg/data/test-25fps.vp9");

  if (ivf_file_header_result.is_error()) {
    FAIL() << ivf_file_header_result.error_value();
  }

  // Ensure the IVF header is what we are expecting
  auto ivf_file_header = ivf_file_header_result.value();
  EXPECT_EQ(0u, ivf_file_header.version);
  EXPECT_EQ(32u, ivf_file_header.header_size);
  EXPECT_EQ(0x30395056u, ivf_file_header.fourcc);  // VP90
  EXPECT_EQ(kVideoWidth, ivf_file_header.width);
  EXPECT_EQ(kVideoHeight, ivf_file_header.height);
  EXPECT_EQ(kExpectedOutputPackets, ivf_file_header.num_frames);

  // Since each decoded frame will be its own output packet, create enough so we don't have to
  // recycle them.
  constexpr uint32_t kOutputPacketCount = 255u;

  constexpr size_t kOutputPacketSize = kVideoBytes;

  ParseIvfFileIntoPackets();
  ConfigureOutputBuffers(kOutputPacketCount, kOutputPacketSize);

  events_.SetBufferInitializationCompleted();
  events_.WaitForInputPacketsDone();
  events_.WaitForOutputPacketCount(kExpectedOutputPackets);

  CodecStreamStop();

  EXPECT_EQ(kExpectedOutputPackets, events_.output_packet_count());
  EXPECT_EQ(0u, events_.fail_codec_count());
  EXPECT_EQ(0u, events_.fail_stream_count());
}

TEST_F(Vp9VaapiTestFixture, DelayedDecode) {
  constexpr uint32_t kExpectedOutputPackets = 250u;

  BlockCodecAndStreamInit();

  auto ivf_file_header_result = InitializeIvfFile("/pkg/data/test-25fps.vp9");

  if (ivf_file_header_result.is_error()) {
    FAIL() << ivf_file_header_result.error_value();
  }

  // Ensure the IVF header is what we are expecting
  auto ivf_file_header = ivf_file_header_result.value();
  EXPECT_EQ(0u, ivf_file_header.version);
  EXPECT_EQ(32u, ivf_file_header.header_size);
  EXPECT_EQ(0x30395056u, ivf_file_header.fourcc);  // VP90
  EXPECT_EQ(kVideoWidth, ivf_file_header.width);
  EXPECT_EQ(kVideoHeight, ivf_file_header.height);
  EXPECT_EQ(kExpectedOutputPackets, ivf_file_header.num_frames);

  // Since each decoded frame will be its own output packet, create enough so we don't have to
  // recycle them.
  constexpr uint32_t kOutputPacketCount = 255u;

  constexpr size_t kOutputPacketSize = kVideoBytes;

  ParseIvfFileIntoPackets();
  ConfigureOutputBuffers(kOutputPacketCount, kOutputPacketSize);

  UnblockInputProcessingLoop();
  events_.SetBufferInitializationCompleted();
  events_.WaitForInputPacketsDone();
  events_.WaitForOutputPacketCount(kExpectedOutputPackets);

  CodecStreamStop();

  EXPECT_EQ(kExpectedOutputPackets, events_.output_packet_count());
  EXPECT_EQ(0u, events_.fail_codec_count());
  EXPECT_EQ(0u, events_.fail_stream_count());
}

TEST_F(Vp9VaapiTestFixture, SkipFirstFrame) {
  // Since we are skipping the first frame these values (which should be the same) diverge
  constexpr uint32_t kExpectedIvfHeaderFrames = 250u;
  constexpr uint32_t kExpectedOutputPackets = 100u;

  BlockCodecAndStreamInit();

  auto ivf_file_header_result = InitializeIvfFile("/pkg/data/test-25fps.vp9");

  if (ivf_file_header_result.is_error()) {
    FAIL() << ivf_file_header_result.error_value();
  }

  // Ensure the IVF header is what we are expecting
  auto ivf_file_header = ivf_file_header_result.value();
  EXPECT_EQ(0u, ivf_file_header.version);
  EXPECT_EQ(32u, ivf_file_header.header_size);
  EXPECT_EQ(0x30395056u, ivf_file_header.fourcc);  // VP90
  EXPECT_EQ(kVideoWidth, ivf_file_header.width);
  EXPECT_EQ(kVideoHeight, ivf_file_header.height);
  EXPECT_EQ(kExpectedIvfHeaderFrames, ivf_file_header.num_frames);

  // Since each decoded frame will be its own output packet, create enough so we don't have to
  // recycle them.
  constexpr uint32_t kOutputPacketCount = 255u;
  constexpr size_t kOutputPacketSize = kVideoBytes;

  // Skip the first packet (keyframe)
  ParseIvfFileIntoPackets(1u);
  ConfigureOutputBuffers(kOutputPacketCount, kOutputPacketSize);

  // Unblock the processing loop once we have added all the input packets. With this test we are
  // ensuring that no data is lost or dropped when the stream is reset after the current frame. The
  // order of the input packets must be maintained and the decoder will recover once a keyframe is
  // encountered again (150 frames after the first frame).
  UnblockInputProcessingLoop();
  events_.SetBufferInitializationCompleted();
  events_.WaitForInputPacketsDone();
  events_.WaitForOutputPacketCount(kExpectedOutputPackets);

  CodecStreamStop();

  EXPECT_EQ(kExpectedOutputPackets, events_.output_packet_count());
  EXPECT_EQ(0u, events_.fail_codec_count());
  EXPECT_EQ(0u, events_.fail_stream_count());
}

TEST(Vp9VaapiTest, Init) {
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
  input_details.set_mime_type("video/vp9");
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

}  // namespace test
