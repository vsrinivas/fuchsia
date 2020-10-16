// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_IMAGE_UTILS_IMAGE_FORMAT_RGBA_H_
#define SRC_CAMERA_LIB_IMAGE_UTILS_IMAGE_FORMAT_RGBA_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <stdint.h>
#include <stdio.h>

namespace camera {

// A convenient way to store RGBA values so they can be converted into various RGBA
// formats.  If this struct is representing color values which have a bit width
// less than 8, the value is shifted to the most significant position.
struct Rgba {
  uint8_t r, g, b, a;

  // Apply bitwise AND to each channel.
  Rgba operator&(const Rgba& mask) const {
    return {static_cast<uint8_t>(r & mask.r), static_cast<uint8_t>(g & mask.g),
            static_cast<uint8_t>(b & mask.b), static_cast<uint8_t>(a & mask.a)};
  }
  bool operator==(const Rgba& other) const {
    return r == other.r && g == other.g && b == other.b && a == other.a;
  }
};

// Converts the bit width (0-8) a field has into a mask
// for that value, assuming it is shifted into the most significant region
// of the byte.
Rgba BitWidthToByteMask(Rgba bit_width);
Rgba Min(const Rgba& a, const Rgba& b);

// Returns the minimum shared bit width between the two formats: |format1| and |format2|.
// Each of the |r|, |g|, |b|, |a| return contain a bit width (0-8) for that channel.
Rgba RgbaMinRes(fuchsia::sysmem::PixelFormatType format1, fuchsia::sysmem::PixelFormatType format2);

// Converts a packed RGB(A) format into 4 individual values.
// For formats that have fewer than 8 bits per color, the color values are shifted
// to their most significant bits.  If the bitwidth of a channel is 0,
// (for example a format with no alpha channel), that value is 0.
Rgba RgbaUnpack(fuchsia::sysmem::PixelFormatType format, uint32_t packed);

// Converts an r, g, b and a value into an RGB(A) format.  The format may be smaller
// than 4 bytes, in which case the packed pixel value occupies to least significant
// position.
uint32_t RgbaPack(fuchsia::sysmem::PixelFormatType format, Rgba in);

std::string ToString(const fuchsia::sysmem::PixelFormatType& type);

// Get the width in bits of each of the components of an RGBA format.
Rgba BitWidth(const fuchsia::sysmem::PixelFormatType& type);

// Return if |type| is a supported format for the above functions.
bool IsSupportedPixelFormat(const fuchsia::sysmem::PixelFormatType& type);

std::vector<fuchsia::sysmem::PixelFormatType> GetSupportedFormats();

fuchsia_sysmem_PixelFormat ConvertPixelFormatToC(const fuchsia::sysmem::PixelFormat& format);
fuchsia_sysmem_ImageFormat_2 ConvertImageFormatToC(const fuchsia::sysmem::ImageFormat_2& format);
}  // namespace camera

#endif  // SRC_CAMERA_LIB_IMAGE_UTILS_IMAGE_FORMAT_RGBA_H_
