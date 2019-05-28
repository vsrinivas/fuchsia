// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <stdint.h>

namespace camera {

// DmaFormat is a local format that is compatible with the sysmem::ImageFormat_2.
// DmaFormat provides a single point of conversion between sysmem and the ISP driver.
class DmaFormat {
    uint8_t pixel_format_ = PixelType::INVALID;
    bool flip_vertical_ = false;
    uint8_t secondary_plane_select_ = 0;

    enum PixelType {
        INVALID = 0,
        RGB32 = 1,
        A2R10G10B10 = 2,
        RGB565 = 3,
        RGB24 = 4,
        GEN32 = 5,
        RAW16 = 6,
        RAW12 = 7,
        AYUV = 8,
        Y410 = 9,
        YUY2 = 10,
        UYVY = 11,
        Y210 = 12,
        NV12 = 13,
        YV12 = 14,
        // The types below are just to specify formats which have
        // different plane_select values.  They will not be used internally.
        NV12_YUV = 13 | (1 << 6),
        NV12_YVU = 13 | (2 << 6),
        NV12_GREY = 13 | (3 << 6),
        YV12_YU = 14 | (1 << 6),
        YV12_YV = 14 | (2 << 6),
    };

public:
    uint32_t width_, height_;

    bool HasSecondaryChannel() const;

    void Set(uint32_t width, uint32_t height, PixelType pixel_format, bool vflip);

    uint32_t BytesPerPixel() const;

    // Get the value that should be written into the line_offset register.
    // Note that the register expects a negative value if the frame is vertically flipped.
    uint32_t GetLineOffset() const;

    // This is added to the address of the memory we are DMAing to.
    uint32_t GetBank0Offset() const;
    uint32_t GetBank0OffsetUv() const;

    uint8_t GetPlaneSelect() const { return 0; }
    uint8_t GetPlaneSelectUv() const;
    uint8_t GetBaseMode() const { return pixel_format_; }
};

} // namespace camera
