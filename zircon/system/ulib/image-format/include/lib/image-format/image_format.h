// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/sysmem/c/fidl.h>

#if defined(__cplusplus)
extern "C" {
#endif

// Iff this returns true, the two pixel formats are equal.
bool ImageFormatIsPixelFormatEqual(const fuchsia_sysmem_PixelFormat& a, const fuchsia_sysmem_PixelFormat& b);

// true - The color_space is potentially compatible with the PixelFormat,
// assuming the correct variant of the ColorSpace is used (with correct bpp).
//
// false - The pixel_format bpp is not supported by the given ColorSpace.  For
// example BT.2100 specifies 10 or 12 bpp, while NV12 specifies 8 bpp, so NV12
// is not compatible with BT.2100.  Or, the system does not support the
// combination of ColorSpace and PixelFormat (even if they are hypothetically
// compatible; in this case support might be added later).
bool ImageFormatIsSupportedColorSpaceForPixelFormat(const fuchsia_sysmem_ColorSpace& color_space, const fuchsia_sysmem_PixelFormat& pixel_format);

// If this returns true, the remainder of the functions in this header can be
// called with pixel_format.  If this returns false, calling any other method of
// this header file may abort() and/or return a meaningless value.
bool ImageFormatIsSupported(const fuchsia_sysmem_PixelFormat* pixel_format);

// Returns the number of bits per pixel for the given PixelFormat.  This is the
// bits per pixel (RGB pixel or Y pixel) in the overall image across all bytes
// that contain pixel data.
//
// For example, NV12 is 12 bits per pixel.  This accounts for sub-sampling in
// both horizontal and vertical.
uint32_t ImageFormatBitsPerPixel(const fuchsia_sysmem_PixelFormat* pixel_format);

// This is the number of stride bytes per pixel of width (RGB pixel width or Y
// pixel width) of plane 0.  For formats such as NV12 that separate Y and UV
// data, this is the number of stride bytes of the Y plane (plane 0).  NV12 has
// the same stride for the UV data.  This function doesn't return stride
// information for planes beyond plane 0.
uint32_t ImageFormatStrideBytesPerWidthPixel(
    const fuchsia_sysmem_PixelFormat* pixel_format);

// This would be height * stride, if it weren't for formats like NV12, where it
// isn't.  The return value is in bytes.
uint64_t ImageFormatImageSize(const fuchsia_sysmem_ImageFormat_2* image_format);

uint32_t ImageFormatCodedWidthMinDivisor(
    const fuchsia_sysmem_PixelFormat* pixel_format);

uint32_t ImageFormatCodedHeightMinDivisor(
    const fuchsia_sysmem_PixelFormat* pixel_format);

// The start of image data must be at least this aligned.
//
// The plane 0 stride is required to be divisible by this alignment.  Generally
// the stride of planes beyond plane 0 (if any) will have a known fixed
// relationship with the plane 0 stride.
uint32_t ImageFormatSampleAlignment(
    const fuchsia_sysmem_PixelFormat* pixel_format);

// Gets the minimum number of bytes per row possible for an image with a
// specific width and specific constraints. Returns false if the width would not
// be valid.
bool ImageFormatMinimumRowBytes(const fuchsia_sysmem_ImageFormatConstraints* constraints,
                                uint32_t width, uint32_t* minimum_row_bytes_out);

#if defined(__cplusplus)
}  // extern "C"
#endif
