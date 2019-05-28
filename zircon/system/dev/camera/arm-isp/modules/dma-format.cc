// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dma-format.h"
#define ALIGN(x, y) (((x) + (y)-1) & -(y))

namespace camera {

uint32_t DmaFormat::BytesPerPixel() const {
    switch (pixel_format_) {
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

bool DmaFormat::HasSecondaryChannel() const {
    return secondary_plane_select_ > 0;
}

void DmaFormat::Set(uint32_t width, uint32_t height, PixelType pixel_format, bool vflip) {
    width_ = width;
    height_ = height;
    flip_vertical_ = vflip;
    pixel_format_ = pixel_format;
    // Collapse all the YV12 and NV12 formats to their base_mode:
    if (pixel_format == PixelType::NV12_YUV ||
        pixel_format == PixelType::NV12_YVU ||
        pixel_format == PixelType::NV12_GREY) {
        pixel_format_ = PixelType::NV12;
    }
    if (pixel_format == PixelType::YV12_YU || pixel_format == PixelType::YV12_YV) {
        pixel_format_ = PixelType::YV12;
    }

    // pull the plane select information into a different variable:
    secondary_plane_select_ = 0;
    if (pixel_format == PixelType::NV12_YVU || pixel_format == PixelType::YV12_YV) {
        secondary_plane_select_ = 2;
    }
    if (pixel_format == PixelType::NV12_YUV || pixel_format == PixelType::YV12_YU) {
        secondary_plane_select_ = 1;
    }
}

uint8_t DmaFormat::GetPlaneSelectUv() const {
    return secondary_plane_select_;
}

// Get the value that should be written into the line_offset register.
// Note that the register expects a negative value if the frame is vertically flipped.
uint32_t DmaFormat::GetLineOffset() const {
    uint32_t line_offset = ALIGN(BytesPerPixel() * width_, 128);
    if (flip_vertical_) {
        return -line_offset;
    }
    return line_offset;
}

// This is added to the address of the memory we are DMAing to.
uint32_t DmaFormat::GetBank0Offset() const {
    if (flip_vertical_) {
        uint32_t line_offset = ALIGN(BytesPerPixel() * width_, 128);
        return (height_ - 1) * line_offset;
    }
    return 0;
}

uint32_t DmaFormat::GetBank0OffsetUv() const {
    // TODO(garratt): Make this actually offset to the correct place in memory
    //                for a buffercollection.
    if (flip_vertical_) {
        uint32_t line_offset = ALIGN(BytesPerPixel() * width_, 128);
        if (pixel_format_ & PixelType::NV12) {
            return (height_ - 2) * line_offset / 2; // UV is half the size
        }
        return (height_ - 1) * line_offset;
    }
    return 0;
}

} // namespace camera
