// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/usb_device/uvc_hack.h"

namespace camera {

// Client buffers are supporting NV12
void UvcHackGetClientBufferImageFormatConstraints(
    fuchsia::sysmem::ImageFormatConstraints* image_format_constraints) {
  image_format_constraints->pixel_format.type = kUvcHackClientPixelFormatType;
  image_format_constraints->color_spaces_count = 1;
  {
    fuchsia::sysmem::ColorSpace color_space;
    color_space.type = kUvcHackClientColorSpaceType;
    image_format_constraints->color_space[0] = std::move(color_space);
  }
  image_format_constraints->min_coded_width = kUvcHackClientCodedWidth;
  image_format_constraints->max_coded_width = kUvcHackClientCodedWidth;
  image_format_constraints->min_coded_height = kUvcHackClientCodedHeight;
  image_format_constraints->max_coded_height = kUvcHackClientCodedHeight;
  image_format_constraints->min_bytes_per_row = kUvcHackClientBytesPerRow;
  image_format_constraints->max_bytes_per_row = kUvcHackClientBytesPerRow;
  image_format_constraints->max_coded_width_times_coded_height =
      kUvcHackClientCodedWidth * kUvcHackClientCodedHeight;
  image_format_constraints->layers = kUvcHackClientLayers;
  image_format_constraints->coded_width_divisor = kUvcHackClientCodedWidthDivisor;
  image_format_constraints->coded_height_divisor = kUvcHackClientCodedHeightDivisor;
  image_format_constraints->bytes_per_row_divisor = kUvcHackClientBytesPerRowDivisor;
  image_format_constraints->start_offset_divisor = kUvcHackClientStartOffsetDivisor;
  image_format_constraints->display_width_divisor = kUvcHackClientDisplayWidthDivisor;
  image_format_constraints->display_height_divisor = kUvcHackClientDisplayHeightDivisor;
  image_format_constraints->required_min_coded_width = kUvcHackClientCodedWidth;
  image_format_constraints->required_max_coded_width = kUvcHackClientCodedWidth;
  image_format_constraints->required_min_coded_height = kUvcHackClientCodedHeight;
  image_format_constraints->required_max_coded_height = kUvcHackClientCodedHeight;
  image_format_constraints->required_min_bytes_per_row = kUvcHackClientBytesPerRow;
  image_format_constraints->required_max_bytes_per_row = kUvcHackClientBytesPerRow;
}

// Client buffers are supporting NV12
void UvcHackGetClientBufferCollectionConstraints(
    fuchsia::sysmem::BufferCollectionConstraints* buffer_collection_constraints) {
  {
    fuchsia::sysmem::BufferUsage usage;
    usage.none = 0;
    usage.cpu = fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageWrite;
    usage.vulkan = 0;
    usage.display = 0;
    usage.video = 0;
    buffer_collection_constraints->usage = std::move(usage);
  }
  buffer_collection_constraints->min_buffer_count_for_camping =
      kUvcHackClientMinBufferCountForCamping;
  buffer_collection_constraints->min_buffer_count_for_dedicated_slack =
      kUvcHackClientMinBufferCountForDedicatedSlack;
  buffer_collection_constraints->min_buffer_count_for_shared_slack =
      kUvcHackClientMinBufferCountForSharedSlack;
  buffer_collection_constraints->min_buffer_count = kUvcHackClientMinBufferCount;
  buffer_collection_constraints->max_buffer_count = kUvcHackClientMaxBufferCount;
  buffer_collection_constraints->has_buffer_memory_constraints = true;
  buffer_collection_constraints->buffer_memory_constraints.ram_domain_supported = true;
  buffer_collection_constraints->buffer_memory_constraints.cpu_domain_supported = true;
  buffer_collection_constraints->image_format_constraints_count = 1;
  {
    fuchsia::sysmem::ImageFormatConstraints image_format_constraints;
    UvcHackGetClientBufferImageFormatConstraints(&image_format_constraints);
    buffer_collection_constraints->image_format_constraints[0] =
        std::move(image_format_constraints);
  }
}

// Client buffers are supporting NV12
void UvcHackGetClientStreamProperties(fuchsia::camera3::StreamProperties* stream_properties) {
  stream_properties->image_format.pixel_format.type = kUvcHackClientPixelFormatType;
  stream_properties->image_format.coded_width = kUvcHackClientCodedWidth;
  stream_properties->image_format.coded_height = kUvcHackClientCodedHeight;
  stream_properties->image_format.bytes_per_row = kUvcHackClientBytesPerRow;
  stream_properties->image_format.display_width = kUvcHackClientDisplayWidth;
  stream_properties->image_format.display_height = kUvcHackClientDisplayHeight;
  stream_properties->image_format.layers = kUvcHackClientLayers;
  stream_properties->image_format.color_space.type = kUvcHackClientColorSpaceType;
  stream_properties->frame_rate.numerator = kUvcHackFrameRateNumerator;
  stream_properties->frame_rate.denominator = kUvcHackFrameRateDenominator;
  stream_properties->supports_crop_region = false;
}

// Client buffers are supporting NV12
void UvcHackGetClientStreamProperties2(fuchsia::camera3::StreamProperties2* stream_properties) {
  stream_properties->mutable_image_format()->pixel_format.type = kUvcHackClientPixelFormatType;
  stream_properties->mutable_image_format()->coded_width = kUvcHackClientCodedWidth;
  stream_properties->mutable_image_format()->coded_height = kUvcHackClientCodedHeight;
  stream_properties->mutable_image_format()->bytes_per_row = kUvcHackClientBytesPerRow;
  stream_properties->mutable_image_format()->display_width = kUvcHackClientDisplayWidth;
  stream_properties->mutable_image_format()->display_height = kUvcHackClientDisplayHeight;
  stream_properties->mutable_image_format()->layers = kUvcHackClientLayers;
  stream_properties->mutable_image_format()->color_space.type = kUvcHackClientColorSpaceType;
  stream_properties->mutable_frame_rate()->numerator = kUvcHackFrameRateNumerator;
  stream_properties->mutable_frame_rate()->denominator = kUvcHackFrameRateDenominator;
  *(stream_properties->mutable_supports_crop_region()) = false;
  {
    fuchsia::math::Size resolution;
    resolution.width = kUvcHackClientCodedWidth;
    resolution.height = kUvcHackClientCodedHeight;
    stream_properties->mutable_supported_resolutions()->push_back(std::move(resolution));
  }
}

// Server buffers are supporting YUY2 (a.k.a. YUYV).
void UvcHackGetServerFrameRate(fuchsia::camera::FrameRate* frame_rate) {
  frame_rate->frames_per_sec_numerator = kUvcHackFrameRateNumerator;
  frame_rate->frames_per_sec_denominator = kUvcHackFrameRateDenominator;
}

// Server buffers are supporting YUY2 (a.k.a. YUYV).
void UvcHackGetServerBufferVideoFormat(fuchsia::camera::VideoFormat* video_format) {
  video_format->format.width = kUvcHackDriverWidth;
  video_format->format.height = kUvcHackDriverHeight;
  video_format->format.layers = kUvcHackDriverLayers;
  video_format->format.pixel_format.type = kUvcHackDriverPixelFormatType;
  video_format->format.pixel_format.has_format_modifier = false;
  video_format->format.color_space.type = kUvcHackDriverColorSpaceType;
  video_format->format.planes[0].byte_offset = 0;
  video_format->format.planes[0].bytes_per_row = kUvcHackDriverBytesPerRow;
  UvcHackGetServerFrameRate(&video_format->rate);
}

// Warning! Grotesquely hard coded for YUY2 server side & NV12 client side.
//
// TODO(ernesthua) - Replace this with libyuv!
void UvcHackConvertYUY2ToNV12(uint8_t* client_frame, const uint8_t* driver_frame) {
  ZX_ASSERT(kUvcHackClientPixelFormatType == fuchsia::sysmem::PixelFormatType::NV12);
  ZX_ASSERT(kUvcHackDriverPixelFormatType == fuchsia::sysmem::PixelFormatType::YUY2);

  ZX_ASSERT(kUvcHackClientCodedWidth == kUvcHackWidth);
  ZX_ASSERT(kUvcHackClientCodedHeight == kUvcHackHeight);
  ZX_ASSERT(kUvcHackDriverWidth == kUvcHackWidth);
  ZX_ASSERT(kUvcHackDriverHeight == kUvcHackHeight);

  ZX_ASSERT((kUvcHackClientBytesPerRow * 2) == kUvcHackDriverBytesPerRow);

  constexpr uint32_t kClientBufferLimit =
      kUvcHackClientBytesPerRow * kUvcHackClientCodedHeight * 3 / 2;
  constexpr uint32_t kClientUVPlaneOffset = kUvcHackClientBytesPerRow * kUvcHackClientCodedHeight;

  constexpr uint32_t kDriverBufferLimit = kUvcHackDriverBytesPerRow * kUvcHackDriverHeight;

  for (uint32_t y = 0; y < kUvcHackHeight; y++) {
    for (uint32_t x = 0; x < kUvcHackWidth; x += 2) {
      uint32_t driver_offset = (y * kUvcHackDriverBytesPerRow) + (x * 2);
      uint32_t client_offset = (y * kUvcHackClientBytesPerRow) + x;
      ZX_ASSERT(driver_offset < kDriverBufferLimit);
      ZX_ASSERT(client_offset < kClientBufferLimit);
      client_frame[client_offset + 0] = driver_frame[driver_offset + 0];
      client_frame[client_offset + 1] = driver_frame[driver_offset + 2];
    }
  }

  for (uint32_t y = 0; y < kUvcHackHeight; y += 2) {
    for (uint32_t x = 0; x < kUvcHackWidth; x += 2) {
      uint32_t driver_offset = (y * kUvcHackDriverBytesPerRow) + (x * 2);
      uint32_t client_offset = kClientUVPlaneOffset + ((y / 2) * kUvcHackClientBytesPerRow) + x;
      ZX_ASSERT(driver_offset < kDriverBufferLimit);
      ZX_ASSERT(client_offset < kClientBufferLimit);
      client_frame[client_offset + 0] = driver_frame[driver_offset + 1];
    }
  }

  for (uint32_t y = 0; y < kUvcHackHeight; y += 2) {
    for (uint32_t x = 0; x < kUvcHackWidth; x += 2) {
      uint32_t driver_offset = (y * kUvcHackDriverBytesPerRow) + (x * 2);
      uint32_t client_offset = kClientUVPlaneOffset + ((y / 2) * kUvcHackClientBytesPerRow) + x;
      ZX_ASSERT(driver_offset < kDriverBufferLimit);
      ZX_ASSERT(client_offset < kClientBufferLimit);
      client_frame[client_offset + 1] = driver_frame[driver_offset + 3];
    }
  }
}

}  // namespace camera
