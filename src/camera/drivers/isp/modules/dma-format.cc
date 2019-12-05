// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dma-format.h"

#include <lib/syslog/global.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/limits.h>

#include <ddk/debug.h>
#include <fbl/algorithm.h>

namespace camera {

constexpr uint32_t kIspLineAlignment = 128;
constexpr auto TAG = "arm-isp";

uint32_t DmaFormat::GetBytesPerPixel() const {
  switch (base_mode_) {
    case PixelType::A2R10G10B10:
    case PixelType::RGB32:
    case PixelType::GEN32:
    case PixelType::AYUV:
    case PixelType::Y410:
    case PixelType::Y210:
      return 4;
    case PixelType::RGB24:
      return 3;
    case PixelType::RGB565:
    case PixelType::RAW16:
    case PixelType::YUY2:
    case PixelType::UYVY:
    case PixelType::RAW12:
      return 2;
    case PixelType::NV12:
    case PixelType::YV12:
      return 1;
  }
  return 0;
}

bool DmaFormat::HasSecondaryChannel() const { return secondary_plane_select_ > 0; }

// TODO(garratt): add more type compatibility to sysmem.
DmaFormat::PixelType ImageFormatToPixelType(const fuchsia_sysmem_ImageFormat& format) {
  switch (format.pixel_format.type) {
    case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
      return DmaFormat::PixelType::RGB32;
    case fuchsia_sysmem_PixelFormatType_NV12:
      return DmaFormat::PixelType::NV12_YUV;
    case fuchsia_sysmem_PixelFormatType_YUY2:
      return DmaFormat::PixelType::YUY2;
  }
  FX_LOG(ERROR, TAG, "pixel_format is incompatible with the ISP's PixelType");
  return DmaFormat::PixelType::INVALID;
}

DmaFormat::PixelType ImageFormatToPixelType(const fuchsia_sysmem_ImageFormat_2& format) {
  switch (format.pixel_format.type) {
    case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
      return DmaFormat::PixelType::RGB32;
    case fuchsia_sysmem_PixelFormatType_NV12:
      return DmaFormat::PixelType::NV12_YUV;
    case fuchsia_sysmem_PixelFormatType_YUY2:
      return DmaFormat::PixelType::YUY2;
  }
  FX_LOG(ERROR, "", "pixel_format is incompatible with the ISP's PixelType\n");
  return DmaFormat::PixelType::INVALID;
}

DmaFormat::DmaFormat(const fuchsia_sysmem_ImageFormat& format)
    : DmaFormat(format.width, format.height, ImageFormatToPixelType(format), false) {}
DmaFormat::DmaFormat(const fuchsia_sysmem_ImageFormat_2& format)
    : DmaFormat(format.coded_width, format.coded_height, ImageFormatToPixelType(format), false) {}

DmaFormat::DmaFormat(uint32_t width, uint32_t height, PixelType pixel_format, bool flip_vertical)
    : width_(width),
      height_(height),
      flip_vertical_(flip_vertical),
      // Use the "base_mode" bits as the pixel format, and plane_select as
      // secondary_plane_select_
      base_mode_(pixel_format & 0x1f),
      secondary_plane_select_((pixel_format & 0xC0) >> kPlaneSelectShift) {
  ZX_ASSERT(pixel_format != PixelType::INVALID);
  // Disallow the NV12 and YV12 types in the constructor; they are only used
  // internally. Use NV12_YUV and YV12_YUV instead.
  ZX_ASSERT(pixel_format != PixelType::NV12);
  ZX_ASSERT(pixel_format != PixelType::YV12);
}

uint8_t DmaFormat::GetPlaneSelect() const { return 0; }

uint8_t DmaFormat::GetPlaneSelectUv() const { return secondary_plane_select_; }

uint8_t DmaFormat::GetBaseMode() const { return base_mode_; }
// Get the value that should be written into the line_offset register.
// Note that the register expects a negative value if the frame is vertically
// flipped.
uint32_t DmaFormat::GetLineOffset() const {
  uint32_t line_offset = fbl::round_up(GetBytesPerPixel() * width_, kIspLineAlignment);
  if (flip_vertical_) {
    return -line_offset;
  }
  return line_offset;
}

// This is added to the address of the memory we are DMAing to.
uint32_t DmaFormat::GetBank0Offset() const {
  if (flip_vertical_) {
    uint32_t line_offset = fbl::round_up(GetBytesPerPixel() * width_, kIspLineAlignment);
    return (height_ - 1) * line_offset;
  }
  return 0;
}

uint32_t DmaFormat::GetBank0OffsetUv() const {
  // Y and UV planes are placed in the same buffer.
  uint32_t line_offset = fbl::round_up(GetBytesPerPixel() * width_, kIspLineAlignment);
  uint32_t primary_offset = fbl::round_up(line_offset * height_, ZX_PAGE_SIZE);
  if (flip_vertical_) {
    if (base_mode_ & PixelType::NV12) {
      return primary_offset + (height_ / 2 - 1) * line_offset;  // UV is half the size
    }
    return primary_offset + (height_ - 1) * line_offset;
  }
  return primary_offset;
}
size_t DmaFormat::GetImageSize() const {
  // lines are aligned to kIspLineAlignment bytes
  uint32_t line_offset = fbl::round_up(GetBytesPerPixel() * width_, kIspLineAlignment);
  // Start with the size of the primary buffer:
  size_t image_size = line_offset * height_;
  // If we have a secondary channel:
  if (base_mode_ == PixelType::NV12) {
    image_size += height_ * line_offset / 2;
  }
  if (base_mode_ == PixelType::YV12) {
    image_size += height_ * line_offset;
  }
  return image_size;
}

}  // namespace camera
