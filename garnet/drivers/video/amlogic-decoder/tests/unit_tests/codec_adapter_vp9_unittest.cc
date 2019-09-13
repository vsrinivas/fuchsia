// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_vp9.h"

#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/media/codec_impl/codec_adapter_events.h>

#include "gtest/gtest.h"

class TestCodecAdapterEvents : public CodecAdapterEvents {
 public:
  void onCoreCodecFailCodec(const char* format, ...) override {}
  void onCoreCodecFailStream(fuchsia::media::StreamError error) override {}
  void onCoreCodecMidStreamOutputConstraintsChange(bool output_re_config_required) override {}
  void onCoreCodecOutputFormatChange() override {}
  void onCoreCodecInputPacketDone(CodecPacket* packet) override {}
  void onCoreCodecOutputPacket(CodecPacket* packet, bool error_detected_before,
                               bool error_detected_during) override {}
  void onCoreCodecOutputEndOfStream(bool error_detected_before) override {}
};

constexpr uint32_t kFrameCount = 5;
constexpr uint32_t kCodedWidth = 5;
constexpr uint32_t kCodedHeight = 100;
constexpr uint32_t kStride = 60;
constexpr uint32_t kDisplayWidth = 4;
constexpr uint32_t kDisplayHeight = 95;
constexpr bool kHasSar = true;
constexpr uint32_t kSarWidth = 1;
constexpr uint32_t kSarHeight = 2;

class CodecAdapterVp9Test : public testing::Test {
 public:
  CodecAdapterVp9Test() : adapter_(lock_, &events_, /*device=*/nullptr) {}

  void InitFrameHandler() {
    adapter_.InitializeFramesHandler(zx::bti(), kFrameCount, kCodedWidth, kCodedHeight, kStride,
                                     kDisplayWidth, kDisplayHeight, kHasSar, kSarWidth, kSarHeight);
  }

 protected:
  std::mutex lock_;
  TestCodecAdapterEvents events_;
  CodecAdapterVp9 adapter_;
};

TEST_F(CodecAdapterVp9Test, OutputFormat) {
  fuchsia::media::StreamOutputFormat format = adapter_.CoreCodecGetOutputFormat(
      /*stream_lifetime_ordinal=*/3, /*new_output_format_details_version_ordinal=*/5);
  const fuchsia::sysmem::ImageFormat_2& image_format =
      format.format_details().domain().video().uncompressed().image_format;

  EXPECT_EQ(image_format.pixel_format.type, fuchsia::sysmem::PixelFormatType::NV12);
  EXPECT_EQ(image_format.coded_width, kCodedWidth);
  EXPECT_EQ(image_format.coded_height, kCodedHeight);
  EXPECT_EQ(image_format.bytes_per_row, kStride);
  EXPECT_EQ(image_format.display_width, kDisplayWidth);
  EXPECT_EQ(image_format.display_height, kDisplayHeight);
  EXPECT_EQ(image_format.layers, 1U);
  EXPECT_EQ(image_format.color_space.type, fuchsia::sysmem::ColorSpaceType::REC709);
  EXPECT_EQ(image_format.has_pixel_aspect_ratio, kHasSar);
  EXPECT_EQ(image_format.pixel_aspect_ratio_width, kSarWidth);
  EXPECT_EQ(image_format.pixel_aspect_ratio_height, kSarHeight);
}

TEST_F(CodecAdapterVp9Test, OutputBufferConstrains) {
  fuchsia::media::StreamBufferConstraints stream_buffer_constraints;
  fuchsia::media::StreamBufferPartialSettings partial_settings;
  partial_settings.set_packet_count_for_server(3);
  partial_settings.set_packet_count_for_client(3);

  fuchsia::sysmem::BufferCollectionConstraints constrains = adapter_.CoreCodecGetBufferCollectionConstraints(
     kOutputPort,stream_buffer_constraints, partial_settings);

  EXPECT_TRUE(constrains.buffer_memory_constraints.cpu_domain_supported);
  EXPECT_TRUE(constrains.buffer_memory_constraints.ram_domain_supported);
  EXPECT_GE(constrains.buffer_memory_constraints.min_size_bytes, kStride * kCodedHeight * 3 /2);
  EXPECT_EQ(constrains.image_format_constraints_count, 1U);
  EXPECT_EQ(constrains.image_format_constraints[0].required_min_coded_width, kCodedWidth);
  EXPECT_EQ(constrains.image_format_constraints[0].required_max_coded_width, kCodedWidth);
  EXPECT_EQ(constrains.image_format_constraints[0].required_min_coded_height, kCodedHeight);
  EXPECT_EQ(constrains.image_format_constraints[0].required_max_coded_height, kCodedHeight);
}
