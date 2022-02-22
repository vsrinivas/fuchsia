// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <gtest/gtest.h>
#include <va/va.h>

#include "src/lib/files/file.h"
#include "src/media/codec/codecs/test/test_codec_packets.h"
#include "src/media/codec/codecs/vaapi/codec_adapter_vaapi_decoder.h"
#include "src/media/codec/codecs/vaapi/vaapi_utils.h"

int vaMaxNumEntrypoints(VADisplay dpy) { return 2; }

VAStatus vaQueryConfigEntrypoints(VADisplay dpy, VAProfile profile, VAEntrypoint *entrypoint_list,
                                  int *num_entrypoints) {
  entrypoint_list[0] = VAEntrypointVLD;
  *num_entrypoints = 1;
  return VA_STATUS_SUCCESS;
}

VAStatus vaGetConfigAttributes(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint,
                               VAConfigAttrib *attrib_list, int num_attribs) {
  EXPECT_EQ(1, num_attribs);
  EXPECT_EQ(VAConfigAttribRTFormat, attrib_list[0].type);
  attrib_list[0].value = VA_RT_FORMAT_YUV420;
  return VA_STATUS_SUCCESS;
}

VAStatus vaDestroyConfig(VADisplay dpy, VAConfigID config_id) {
  return VA_STATUS_ERROR_FLAG_NOT_SUPPORTED;
}

VAStatus vaCreateConfig(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint,
                        VAConfigAttrib *attrib_list, int num_attribs, VAConfigID *config_id) {
  *config_id = 1;
  return VA_STATUS_SUCCESS;
}

VAStatus vaQueryConfigAttributes(VADisplay dpy, VAConfigID config_id, VAProfile *profile,
                                 VAEntrypoint *entrypoint, VAConfigAttrib *attrib_list,
                                 int *num_attribs) {
  return VA_STATUS_ERROR_FLAG_NOT_SUPPORTED;
}

VAStatus vaCreateSurfaces(VADisplay dpy, unsigned int format, unsigned int width,
                          unsigned int height, VASurfaceID *surfaces, unsigned int num_surfaces,
                          VASurfaceAttrib *attrib_list, unsigned int num_attribs) {
  for (size_t i = 0; i < num_surfaces; i++) {
    surfaces[i] = static_cast<unsigned int>(i + 1);
  }
  return VA_STATUS_SUCCESS;
}

VAStatus vaDestroySurfaces(VADisplay dpy, VASurfaceID *surfaces, int num_surfaces) {
  return VA_STATUS_SUCCESS;
}

VAStatus vaCreateContext(VADisplay dpy, VAConfigID config_id, int picture_width, int picture_height,
                         int flag, VASurfaceID *render_targets, int num_render_targets,
                         VAContextID *context) {
  *context = 1;
  return VA_STATUS_SUCCESS;
}

VAStatus vaDestroyContext(VADisplay dpy, VAContextID context) { return VA_STATUS_SUCCESS; }

VAStatus vaBeginPicture(VADisplay dpy, VAContextID context, VASurfaceID render_target) {
  return VA_STATUS_SUCCESS;
}

VAStatus vaRenderPicture(VADisplay dpy, VAContextID context, VABufferID *buffers, int num_buffers) {
  return VA_STATUS_SUCCESS;
}

VAStatus vaEndPicture(VADisplay dpy, VAContextID context) { return VA_STATUS_SUCCESS; }

VAStatus vaSyncSurface(VADisplay dpy, VASurfaceID render_target) { return VA_STATUS_SUCCESS; }

VAStatus vaGetImage(VADisplay dpy, VASurfaceID surface, int x, int y, unsigned int width,
                    unsigned int height, VAImageID image) {
  return VA_STATUS_SUCCESS;
}

VAStatus vaDeriveImage(VADisplay dpy, VASurfaceID surface, VAImage *image) {
  return VA_STATUS_SUCCESS;
}

VAStatus vaDestroyImage(VADisplay dpy, VAImageID image) { return VA_STATUS_SUCCESS; }

VAStatus vaCreateBuffer(VADisplay dpy, VAContextID context, VABufferType type, unsigned int size,
                        unsigned int num_elements, void *data, VABufferID *buf_id) {
  return VA_STATUS_SUCCESS;
}

VAStatus vaDestroyBuffer(VADisplay dpy, VABufferID buffer_id) { return VA_STATUS_SUCCESS; }

VAStatus vaInitialize(VADisplay dpy, int *major_version, int *minor_version) {
  *major_version = VA_MAJOR_VERSION;
  *minor_version = VA_MINOR_VERSION;
  return VA_STATUS_SUCCESS;
}

static int global_display_ptr;

VADisplay vaGetDisplayMagma(magma_device_t device) { return &global_display_ptr; }

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
  }

  void onCoreCodecFailStream(fuchsia::media::StreamError error) override {
    printf("Got onCoreCodecFailStream %d\n", static_cast<int>(error));
    fflush(stdout);
    fail_stream_count_++;
  }

  void onCoreCodecResetStreamAfterCurrentFrame() override {}

  void onCoreCodecMidStreamOutputConstraintsChange(bool output_re_config_required) override {
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
    std::lock_guard lock(lock_);
    output_packet_count_++;
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
    cond_.wait(lock, [&]() { return output_packet_count_ == output_packet_count; });
  }

  size_t output_packet_count() const { return output_packet_count_; }

  void SetBufferInitializationCompleted() {
    std::lock_guard lock(lock_);
    buffer_initialization_completed_ = true;
    cond_.notify_all();
  }

 private:
  CodecAdapter *codec_adapter_ = nullptr;
  uint64_t fail_codec_count_{};
  uint64_t fail_stream_count_{};

  std::mutex lock_;
  std::condition_variable cond_;

  std::vector<CodecPacket *> input_packets_done_;
  size_t output_packet_count_ = 0;
  bool buffer_initialization_completed_ = false;
};

TEST(H264Vaapi, DecodeBasic) {
  std::mutex lock;

  EXPECT_TRUE(VADisplayWrapper::InitializeSingletonForTesting());

  FakeCodecAdapterEvents events;
  {
    CodecAdapterVaApiDecoder decoder(lock, &events);
    events.set_codec_adapter(&decoder);
    fuchsia::media::FormatDetails format_details;
    format_details.set_format_details_version_ordinal(1);
    format_details.set_mime_type("video/h264");
    decoder.CoreCodecInit(format_details);

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
      packet->SetBuffer(test_buffers.buffers[i].get());
      packet->SetStartOffset(0);
      packet->SetValidLengthBytes(kOutputPacketSize);
      packets[i] = std::move(packet);
      decoder.CoreCodecAddBuffer(CodecPort::kOutputPort, test_buffers.buffers[i].get());
    }

    decoder.CoreCodecConfigureBuffers(CodecPort::kOutputPort, packets);
    events.SetBufferInitializationCompleted();
    events.WaitForInputPacketsDone();
    decoder.CoreCodecStopStream();
  }

  EXPECT_EQ(0u, events.fail_codec_count());
  EXPECT_EQ(0u, events.fail_stream_count());
}

TEST(H264Vaapi, CodecList) {
  EXPECT_TRUE(VADisplayWrapper::InitializeSingletonForTesting());
  auto codec_list = GetCodecList();
  EXPECT_EQ(2u, codec_list.size());
}

}  // namespace
