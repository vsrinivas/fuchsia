// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/image-format/image_format.h"

#include <zircon/assert.h>

#include <map>
#include <set>

namespace {

// There are two aspects of the ColorSpace and PixelFormat that we care about:
//   * bits-per-sample - bits per primary sample (R, G, B, or Y)
//   * RGB vs. YUV - whether the system supports the ColorSpace or PixelFormat
//     representing RGB data or YUV data.  Any given ColorSpace only supports
//     one or the other. Currently any given PixelFormat only supports one or
//     the other and this isn't likely to change.
// While we could just list all the ColorSpace(s) that each PixelFormat could
// plausibly support, expressing in terms of bits-per-sample and RGB vs. YUV is
// perhaps easier to grok.

enum ColorType {
    kColorType_NONE,
    kColorType_RGB,
    kColorType_YUV
};

struct SamplingInfo {
    std::set<uint32_t> possible_bits_per_sample;
    ColorType color_type;
};

const std::map<fuchsia_sysmem_ColorSpaceType, SamplingInfo> kColorSpaceSamplingInfo = {
    {fuchsia_sysmem_ColorSpaceType_SRGB, {{8, 10, 12, 16}, kColorType_RGB}},
    {fuchsia_sysmem_ColorSpaceType_REC601_NTSC, {{8, 10}, kColorType_YUV}},
    {fuchsia_sysmem_ColorSpaceType_REC601_NTSC_FULL_RANGE, {{8, 10}, kColorType_YUV}},
    {fuchsia_sysmem_ColorSpaceType_REC601_PAL, {{8, 10}, kColorType_YUV}},
    {fuchsia_sysmem_ColorSpaceType_REC601_PAL_FULL_RANGE, {{8, 10}, kColorType_YUV}},
    {fuchsia_sysmem_ColorSpaceType_REC709, {{8, 10}, kColorType_YUV}},
    {fuchsia_sysmem_ColorSpaceType_REC2020, {{10, 12}, kColorType_YUV}},
    {fuchsia_sysmem_ColorSpaceType_REC2100, {{10, 12}, kColorType_YUV}},
};
const std::map<fuchsia_sysmem_PixelFormatType, SamplingInfo> kPixelFormatSamplingInfo = {
    {fuchsia_sysmem_PixelFormatType_R8G8B8A8, {{8}, kColorType_RGB}},
    {fuchsia_sysmem_PixelFormatType_BGRA32, {{8}, kColorType_RGB}},
    {fuchsia_sysmem_PixelFormatType_I420, {{8}, kColorType_YUV}},
    {fuchsia_sysmem_PixelFormatType_M420, {{8}, kColorType_YUV}},
    {fuchsia_sysmem_PixelFormatType_NV12, {{8}, kColorType_YUV}},
    {fuchsia_sysmem_PixelFormatType_YUY2, {{8}, kColorType_YUV}},
    // 8 bits RGB when uncompressed - in this context, MJPEG is essentially
    // pretending to be uncompressed.
    {fuchsia_sysmem_PixelFormatType_MJPEG, {{8}, kColorType_RGB}},
};

}  // namespace

bool ImageFormatIsPixelFormatEqual(const fuchsia_sysmem_PixelFormat& a, const fuchsia_sysmem_PixelFormat& b) {
    return
        a.type == b.type &&
        // !has_format_modifier is for consistency with making format_modifier
        // optional in future.
        a.has_format_modifier == b.has_format_modifier &&
        // Must be 0 if !has_format_modifier.
        a.format_modifier.value == b.format_modifier.value;
}

bool ImageFormatIsSupportedColorSpaceForPixelFormat(const fuchsia_sysmem_ColorSpace& color_space, const fuchsia_sysmem_PixelFormat& pixel_format) {
    // Not yet supported.
    ZX_DEBUG_ASSERT(!pixel_format.has_format_modifier);
    auto color_space_sampling_info_iter = kColorSpaceSamplingInfo.find(color_space.type);
    if (color_space_sampling_info_iter == kColorSpaceSamplingInfo.end()) {
        return false;
    }
    auto pixel_format_sampling_info_iter = kPixelFormatSamplingInfo.find(pixel_format.type);
    if (pixel_format_sampling_info_iter == kPixelFormatSamplingInfo.end()) {
        return false;
    }
    const SamplingInfo& color_space_sampling_info = color_space_sampling_info_iter->second;
    const SamplingInfo& pixel_format_sampling_info = pixel_format_sampling_info_iter->second;
    if (color_space_sampling_info.color_type != pixel_format_sampling_info.color_type) {
        return false;
    }
    bool is_bits_per_sample_match_found = false;
    for (uint32_t bits_per_sample : color_space_sampling_info.possible_bits_per_sample) {
        auto pixel_format_bits_per_sample_iter = pixel_format_sampling_info.possible_bits_per_sample.find(bits_per_sample);
        if (pixel_format_bits_per_sample_iter != pixel_format_sampling_info.possible_bits_per_sample.end()) {
            is_bits_per_sample_match_found = true;
            break;
        }
    }
    if (!is_bits_per_sample_match_found) {
        return false;
    }
    return true;
}

bool ImageFormatIsSupported(const fuchsia_sysmem_PixelFormat* pixel_format) {
    if (pixel_format->has_format_modifier) {
        return false;
    }
    switch (pixel_format->type) {
        case fuchsia_sysmem_PixelFormatType_INVALID:
        case fuchsia_sysmem_PixelFormatType_MJPEG:
            return false;
        case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
        case fuchsia_sysmem_PixelFormatType_BGRA32:
        case fuchsia_sysmem_PixelFormatType_I420:
        case fuchsia_sysmem_PixelFormatType_M420:
        case fuchsia_sysmem_PixelFormatType_NV12:
        case fuchsia_sysmem_PixelFormatType_YUY2:
            return true;
    }
    return false;
}

// Overall bits per pixel, across all pixel data in the whole image.
uint32_t ImageFormatBitsPerPixel(const fuchsia_sysmem_PixelFormat* pixel_format) {
    ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
    switch (pixel_format->type) {
        case fuchsia_sysmem_PixelFormatType_INVALID:
        case fuchsia_sysmem_PixelFormatType_MJPEG:
            // impossible; checked previously.
            ZX_DEBUG_ASSERT(false);
            return 0u;
        case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
            return 4u * 8u;
        case fuchsia_sysmem_PixelFormatType_BGRA32:
            return 4u * 8u;
        case fuchsia_sysmem_PixelFormatType_I420:
            return 12u;
        case fuchsia_sysmem_PixelFormatType_M420:
            return 12u;
        case fuchsia_sysmem_PixelFormatType_NV12:
            return 12u;
        case fuchsia_sysmem_PixelFormatType_YUY2:
            return 2u * 8u;
    }
    ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format->type));
    return 0u;
}

uint32_t ImageFormatStrideBytesPerWidthPixel(
    const fuchsia_sysmem_PixelFormat* pixel_format) {
    ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
    switch (pixel_format->type) {
        case fuchsia_sysmem_PixelFormatType_INVALID:
        case fuchsia_sysmem_PixelFormatType_MJPEG:
            // impossible; checked previously.
            ZX_DEBUG_ASSERT(false);
            return 0u;
        case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
            return 4u;
        case fuchsia_sysmem_PixelFormatType_BGRA32:
            return 4u;
        case fuchsia_sysmem_PixelFormatType_I420:
            return 1u;
        case fuchsia_sysmem_PixelFormatType_M420:
            return 1u;
        case fuchsia_sysmem_PixelFormatType_NV12:
            return 1u;
        case fuchsia_sysmem_PixelFormatType_YUY2:
            return 2u;
    }
    ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format->type));
    return 0u;
}

uint64_t ImageFormatImageSize(const fuchsia_sysmem_ImageFormat_2* image_format) {
    ZX_DEBUG_ASSERT(ImageFormatIsSupported(&image_format->pixel_format));
    uint64_t coded_height = image_format->coded_height;
    uint64_t bytes_per_row = image_format->bytes_per_row;
    switch (image_format->pixel_format.type) {
        case fuchsia_sysmem_PixelFormatType_INVALID:
        case fuchsia_sysmem_PixelFormatType_MJPEG:
            // impossible; checked previously.
            ZX_DEBUG_ASSERT(false);
            return 0u;
        case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
        case fuchsia_sysmem_PixelFormatType_BGRA32:
            return coded_height * bytes_per_row;
        case fuchsia_sysmem_PixelFormatType_I420:
            return coded_height * bytes_per_row * 3 / 2;
        case fuchsia_sysmem_PixelFormatType_M420:
            return coded_height * bytes_per_row * 3 / 2;
        case fuchsia_sysmem_PixelFormatType_NV12:
            return coded_height * bytes_per_row * 3 / 2;
        case fuchsia_sysmem_PixelFormatType_YUY2:
            return coded_height * bytes_per_row;
    }
    ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(image_format->pixel_format.type));
    return 0u;
}

uint32_t ImageFormatCodedWidthMinDivisor(
    const fuchsia_sysmem_PixelFormat* pixel_format) {
    ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
    switch (pixel_format->type) {
        case fuchsia_sysmem_PixelFormatType_INVALID:
        case fuchsia_sysmem_PixelFormatType_MJPEG:
            // impossible; checked previously.
            ZX_DEBUG_ASSERT(false);
            return 0u;
        case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
            return 1u;
        case fuchsia_sysmem_PixelFormatType_BGRA32:
            return 1u;
        case fuchsia_sysmem_PixelFormatType_I420:
            return 2u;
        case fuchsia_sysmem_PixelFormatType_M420:
            return 2u;
        case fuchsia_sysmem_PixelFormatType_NV12:
            return 2u;
        case fuchsia_sysmem_PixelFormatType_YUY2:
            return 2u;
    }
    ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format->type));
    return 0u;
}

uint32_t ImageFormatCodedHeightMinDivisor(
    const fuchsia_sysmem_PixelFormat* pixel_format) {
    ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
    switch (pixel_format->type) {
        case fuchsia_sysmem_PixelFormatType_INVALID:
        case fuchsia_sysmem_PixelFormatType_MJPEG:
            // impossible; checked previously.
            ZX_DEBUG_ASSERT(false);
            return 0u;
        case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
            return 1u;
        case fuchsia_sysmem_PixelFormatType_BGRA32:
            return 1u;
        case fuchsia_sysmem_PixelFormatType_I420:
            return 2u;
        case fuchsia_sysmem_PixelFormatType_M420:
            return 2u;
        case fuchsia_sysmem_PixelFormatType_NV12:
            return 2u;
        case fuchsia_sysmem_PixelFormatType_YUY2:
            return 2u;
    }
    ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format->type));
    return 0u;
}

uint32_t ImageFormatSampleAlignment(
    const fuchsia_sysmem_PixelFormat* pixel_format) {
    ZX_DEBUG_ASSERT(ImageFormatIsSupported(pixel_format));
    switch (pixel_format->type) {
        case fuchsia_sysmem_PixelFormatType_INVALID:
        case fuchsia_sysmem_PixelFormatType_MJPEG:
            // impossible; checked previously.
            ZX_DEBUG_ASSERT(false);
            return 0u;
        case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
            return 4u;
        case fuchsia_sysmem_PixelFormatType_BGRA32:
            return 4u;
        case fuchsia_sysmem_PixelFormatType_I420:
            return 2u;
        case fuchsia_sysmem_PixelFormatType_M420:
            return 2u;
        case fuchsia_sysmem_PixelFormatType_NV12:
            return 2u;
        case fuchsia_sysmem_PixelFormatType_YUY2:
            return 2u;
    }
    ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format->type));
    return 0u;
}
