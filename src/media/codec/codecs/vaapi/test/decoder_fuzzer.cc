// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "decoder_fuzzer.h"

#include <fuzzer/FuzzedDataProvider.h>

static int global_display_ptr;

VADisplay vaGetDisplayMagma(magma_device_t device) { return &global_display_ptr; }

void FakeCodecAdapterEvents::onCoreCodecFailCodec(const char *format, ...) {
  va_list args;
  va_start(args, format);
  printf("Got onCoreCodecFailCodec: ");
  vprintf(format, args);
  printf("\n");
  fflush(stdout);
  va_end(args);

  std::unique_lock<std::mutex> lock(lock_);
  fail_codec_count_++;
  cond_.notify_all();
}

void FakeCodecAdapterEvents::onCoreCodecFailStream(fuchsia::media::StreamError error) {
  printf("Got onCoreCodecFailStream %d\n", static_cast<int>(error));
  fflush(stdout);
  std::unique_lock<std::mutex> lock(lock_);
  fail_stream_count_++;
  cond_.notify_all();
}

void FakeCodecAdapterEvents::onCoreCodecResetStreamAfterCurrentFrame() {}

void FakeCodecAdapterEvents::onCoreCodecMidStreamOutputConstraintsChange(
    bool output_re_config_required) {
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
  codec_adapter_->CoreCodecSetBufferCollectionInfo(CodecPort::kOutputPort, buffer_collection);
  codec_adapter_->CoreCodecMidStreamOutputBufferReConfigFinish();
}

void FakeCodecAdapterEvents::onCoreCodecOutputFormatChange() {}

void FakeCodecAdapterEvents::onCoreCodecInputPacketDone(CodecPacket *packet) {
  std::lock_guard lock(lock_);
  input_packets_done_.push_back(packet);
  cond_.notify_all();
}

void FakeCodecAdapterEvents::onCoreCodecOutputPacket(CodecPacket *packet,
                                                     bool error_detected_before,
                                                     bool error_detected_during) {
  auto output_format = codec_adapter_->CoreCodecGetOutputFormat(1u, 1u);
}

void FakeCodecAdapterEvents::onCoreCodecOutputEndOfStream(bool error_detected_before) {
  printf("Got onCoreCodecOutputEndOfStream\n");
  fflush(stdout);
}

void FakeCodecAdapterEvents::onCoreCodecLogEvent(
    media_metrics::StreamProcessorEvents2MetricDimensionEvent event_code) {}

void FakeCodecAdapterEvents::SetBufferInitializationCompleted() {
  std::lock_guard lock(lock_);
  buffer_initialization_completed_ = true;
  cond_.notify_all();
}

void FakeCodecAdapterEvents::WaitForIdle(size_t input_packet_count) {
  std::unique_lock<std::mutex> lock(lock_);
  cond_.wait_for(lock, std::chrono::milliseconds(50), [&]() {
    return fail_codec_count_ > 0 || fail_stream_count_ > 0 ||
           input_packets_done_.size() == input_packet_count;
  });
}

VaapiFuzzerTestFixture::~VaapiFuzzerTestFixture() { decoder_.reset(); }

void VaapiFuzzerTestFixture::SetUp() {
  EXPECT_TRUE(VADisplayWrapper::InitializeSingletonForTesting());

  vaDefaultStubSetReturn();

  // Have to defer the construction of decoder_ until
  // VADisplayWrapper::InitializeSingletonForTesting is called
  decoder_ = std::make_unique<CodecAdapterVaApiDecoder>(lock_, &events_);
  events_.set_codec_adapter(decoder_.get());
}

void VaapiFuzzerTestFixture::CodecAndStreamInit(std::string mime_type) {
  fuchsia::media::FormatDetails format_details;
  format_details.set_format_details_version_ordinal(1);
  format_details.set_mime_type(mime_type);
  decoder_->CoreCodecInit(format_details);

  auto input_constraints = decoder_->CoreCodecGetBufferCollectionConstraints(
      CodecPort::kInputPort, fuchsia::media::StreamBufferConstraints(),
      fuchsia::media::StreamBufferPartialSettings());
  EXPECT_TRUE(input_constraints.buffer_memory_constraints.cpu_domain_supported);

  decoder_->CoreCodecStartStream();
  decoder_->CoreCodecQueueInputFormatDetails(format_details);
}

void VaapiFuzzerTestFixture::CodecStreamStop() {
  decoder_->CoreCodecStopStream();
  decoder_->CoreCodecEnsureBuffersNotConfigured(CodecPort::kOutputPort);
}

void VaapiFuzzerTestFixture::ParseDataIntoInputPackets(const uint8_t *data, size_t size) {
  FuzzedDataProvider provider(data, size);

  constexpr uint32_t kMaxInputPackets = 32;
  uint32_t input_packets = 0;

  while ((input_packets < kMaxInputPackets) && (provider.remaining_bytes() > 0)) {
    std::string str = provider.ConsumeRandomLengthString(std::numeric_limits<uint32_t>::max());

    // CodecImpl validates that the size > 0.
    if (!str.empty()) {
      auto input_buffer = std::make_unique<CodecBufferForTest>(str.size(), 0, false);
      std::memcpy(input_buffer->base(), str.data(), str.size());

      auto input_packet = std::make_unique<CodecPacketForTest>(input_packets);
      input_packet->SetStartOffset(0);
      input_packet->SetValidLengthBytes(static_cast<uint32_t>(str.size()));
      input_packet->SetBuffer(input_buffer.get());
      decoder_->CoreCodecQueueInputPacket(input_packet.get());
      input_buffers_.push_back(std::move(input_buffer));
      input_packets_.push_back(std::move(input_packet));

      input_packets += 1;
    }
  }
}

void VaapiFuzzerTestFixture::ConfigureOutputBuffers(uint32_t output_packet_count,
                                                    size_t output_packet_size) {
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

void VaapiFuzzerTestFixture::RunFuzzer(std::string mime_type, const uint8_t *data, size_t size) {
  CodecAndStreamInit(mime_type);

  // Should be enough to handle a large fraction of bear.h264 output without recycling.
  constexpr uint32_t kOutputPacketCount = 35;
  // Nothing writes to the output packet so its size doesn't matter.
  constexpr size_t kOutputPacketSize = 4096;

  ParseDataIntoInputPackets(data, size);
  ConfigureOutputBuffers(kOutputPacketCount, kOutputPacketSize);

  events_.SetBufferInitializationCompleted();
  events_.WaitForIdle(input_packets_.size());

  // Wait a tiny bit more to increase the chance of detecting teardown issues.
  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  CodecStreamStop();
}
