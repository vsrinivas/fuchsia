// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_codec_adapter.h"

#include <lib/media/codec_impl/fourcc.h>
#include <zircon/assert.h>

#include <limits>

#include "fuchsia/sysmem/cpp/fidl.h"

namespace {

// We use "video/raw" for output since for now it makes sense to pretend to be a
// video decoder.
constexpr const char* kOutputMimeType = "video/raw";
constexpr uint32_t kFourccRgba = make_fourcc('R', 'G', 'B', 'A');
constexpr uint32_t kCodedWidth = 256;
constexpr uint32_t kCodedHeight = 144;
constexpr uint32_t kPixelStride = sizeof(uint32_t);
constexpr uint32_t kBytesPerRow = kCodedWidth * kPixelStride;
constexpr uint32_t kDisplayWidth = kCodedWidth;
constexpr uint32_t kDisplayHeight = kCodedHeight;
constexpr uint32_t kLayers = 1;

constexpr uint32_t kInputMinBufferCountForCamping = 1;
constexpr uint32_t kOutputMinBufferCountForCamping = 5;

constexpr uint32_t kPerPacketBufferBytesMin = kBytesPerRow * kCodedHeight;
constexpr uint32_t kPacketCountForServerMin = 1;
constexpr uint32_t kPacketCountForServerRecommended = 1;
constexpr uint32_t kPacketCountForServerMax = 1;
constexpr uint32_t kPacketCountForClientMin = 1;
constexpr uint32_t kPacketCountForClientMax = 1;

constexpr uint32_t kPacketCountForServerDefault = kPacketCountForServerRecommended;
constexpr uint32_t kPacketCountForClientDefault = 1;

}  // namespace

FakeCodecAdapter::FakeCodecAdapter(std::mutex& lock, CodecAdapterEvents* codec_adapter_events)
    : CodecAdapter(lock, codec_adapter_events) {
  // nothing else to do here
}

FakeCodecAdapter::~FakeCodecAdapter() {
  // nothing to do here
}

bool FakeCodecAdapter::IsCoreCodecRequiringOutputConfigForFormatDetection() {
  // To cause CoreCodecBuildNewOutputConstraints() to get called.
  return true;
}

bool FakeCodecAdapter::IsCoreCodecMappedBufferUseful(CodecPort port) { return true; }

bool FakeCodecAdapter::IsCoreCodecHwBased(CodecPort port) { return false; }

void FakeCodecAdapter::CoreCodecInit(
    const fuchsia::media::FormatDetails& initial_input_format_details) {
  // nothing to do here
}

fuchsia::sysmem::BufferCollectionConstraints
FakeCodecAdapter::CoreCodecGetBufferCollectionConstraints(
    CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings) {
  // If test harness has set an override, just return that.
  if (buffer_collection_constraints_[port]) {
    return fidl::Clone(*buffer_collection_constraints_[port]);
  }

  ZX_DEBUG_ASSERT(false);
  fuchsia::sysmem::BufferCollectionConstraints result;
  ZX_DEBUG_ASSERT(result.usage.cpu == 0);
  ZX_DEBUG_ASSERT(result.usage.display == 0);
  ZX_DEBUG_ASSERT(result.usage.video == 0);
  ZX_DEBUG_ASSERT(result.usage.vulkan == 0);
  if (port == kInputPort) {
    result.min_buffer_count_for_camping = kInputMinBufferCountForCamping;
  } else {
    ZX_DEBUG_ASSERT(port == kOutputPort);
    result.min_buffer_count_for_camping = kOutputMinBufferCountForCamping;
  }
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_dedicated_slack == 0);
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_shared_slack == 0);
  ZX_DEBUG_ASSERT(result.min_buffer_count == 0);
  // 0 is treated as 0xFFFFFFFF.
  ZX_DEBUG_ASSERT(result.max_buffer_count == 0);
  result.has_buffer_memory_constraints = true;
  if (port == kInputPort) {
    // Despite the defaults being fine for the fake, CodecImpl wants this bool
    // set to true.  All real CodecAdapter implementations will likely want to
    // have some constraints on buffer size - or if they don't, they can also
    // just set has_buffer_memory_constraints true with defaults for all fields.
    ZX_DEBUG_ASSERT(result.has_buffer_memory_constraints);
  } else {
    result.buffer_memory_constraints.min_size_bytes = kPerPacketBufferBytesMin;
    ZX_DEBUG_ASSERT(result.buffer_memory_constraints.cpu_domain_supported);
  }
  ZX_DEBUG_ASSERT(result.image_format_constraints_count == 0);
  return result;
}

void FakeCodecAdapter::CoreCodecSetBufferCollectionInfo(
    CodecPort port, const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  // nothing to do here
}

void FakeCodecAdapter::CoreCodecStartStream() {
  // nothing to do here
}

void FakeCodecAdapter::CoreCodecQueueInputFormatDetails(
    const fuchsia::media::FormatDetails& per_stream_override_format_details) {
  // nothing to do here
}

void FakeCodecAdapter::CoreCodecQueueInputPacket(CodecPacket* packet) {
  // nothing to do here
}

void FakeCodecAdapter::CoreCodecQueueInputEndOfStream() {
  // nothing to do here
}

void FakeCodecAdapter::CoreCodecStopStream() {
  // nothing to do here
}

void FakeCodecAdapter::CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) {
  // nothing to do here
}

void FakeCodecAdapter::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  // nothing to do here
}

void FakeCodecAdapter::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  // nothing to do here
}

void FakeCodecAdapter::CoreCodecEnsureBuffersNotConfigured(CodecPort port) {
  // nothing to do here
}

std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
FakeCodecAdapter::CoreCodecBuildNewOutputConstraints(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
    bool buffer_constraints_action_required) {
  fuchsia::media::StreamOutputConstraints result;
  result.set_stream_lifetime_ordinal(stream_lifetime_ordinal);
  result.set_buffer_constraints_action_required(buffer_constraints_action_required);
  result.mutable_buffer_constraints()
      ->set_buffer_constraints_version_ordinal(new_output_buffer_constraints_version_ordinal)
      .set_per_packet_buffer_bytes_min(kPerPacketBufferBytesMin)
      .set_packet_count_for_server_min(kPacketCountForServerMin)
      .set_packet_count_for_server_recommended(kPacketCountForServerRecommended)
      .set_packet_count_for_server_recommended_max(kPacketCountForServerMax)
      .set_packet_count_for_server_max(kPacketCountForServerMax)
      .set_packet_count_for_client_min(kPacketCountForClientMin)
      .set_packet_count_for_client_max(kPacketCountForClientMax)
      .set_single_buffer_mode_allowed(false)
      .set_is_physically_contiguous_required(false);
  result.mutable_buffer_constraints()
      ->mutable_default_settings()
      ->set_buffer_constraints_version_ordinal(new_output_buffer_constraints_version_ordinal)
      .set_packet_count_for_server(kPacketCountForServerDefault)
      .set_packet_count_for_client(kPacketCountForClientDefault)
      .set_per_packet_buffer_bytes(kPerPacketBufferBytesMin)
      .set_single_buffer_mode(false);
  return std::make_unique<const fuchsia::media::StreamOutputConstraints>(std::move(result));
}

fuchsia::media::StreamOutputFormat FakeCodecAdapter::CoreCodecGetOutputFormat(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_format_details_version_ordinal) {
  fuchsia::media::StreamOutputFormat result;
  result.set_stream_lifetime_ordinal(stream_lifetime_ordinal);
  result.mutable_format_details()
      ->set_format_details_version_ordinal(new_output_format_details_version_ordinal)
      .set_mime_type(kOutputMimeType);
  fuchsia::media::VideoFormat video_format;
  fuchsia::media::VideoUncompressedFormat video_uncompressed;
  fuchsia::sysmem::ImageFormat_2* image_format = &video_uncompressed.image_format;
  image_format->pixel_format.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8;
  image_format->color_space.type = fuchsia::sysmem::ColorSpaceType::SRGB;
  image_format->coded_width = kCodedWidth;
  image_format->coded_height = kCodedHeight;
  image_format->bytes_per_row = kBytesPerRow;
  image_format->display_width = kDisplayWidth;
  image_format->display_height = kDisplayHeight;
  image_format->layers = kLayers;
  video_uncompressed.fourcc = kFourccRgba;
  video_uncompressed.primary_width_pixels = kCodedWidth;
  video_uncompressed.primary_height_pixels = kCodedHeight;
  video_uncompressed.primary_line_stride_bytes = kBytesPerRow;
  video_uncompressed.primary_pixel_stride = kPixelStride;
  video_uncompressed.primary_display_width_pixels = kDisplayWidth;
  video_uncompressed.primary_display_height_pixels = kDisplayHeight;
  video_format.set_uncompressed(std::move(video_uncompressed));
  result.mutable_format_details()->mutable_domain()->set_video(std::move(video_format));
  return result;
}

void FakeCodecAdapter::CoreCodecMidStreamOutputBufferReConfigPrepare() {
  // nothing to do here
}

void FakeCodecAdapter::CoreCodecMidStreamOutputBufferReConfigFinish() {
  // nothing to do here
}

void FakeCodecAdapter::SetBufferCollectionConstraints(
    CodecPort port, fuchsia::sysmem::BufferCollectionConstraints constraints) {
  buffer_collection_constraints_[port] = std::move(constraints);
}
