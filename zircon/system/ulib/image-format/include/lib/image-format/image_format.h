// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_IMAGE_FORMAT_IMAGE_FORMAT_H_
#define LIB_IMAGE_FORMAT_IMAGE_FORMAT_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <zircon/pixelformat.h>

// TODO(dustingreen): Also accept all the v2 Table::Builder(s).

// Iff this returns true, the two pixel formats are equal.
bool ImageFormatIsPixelFormatEqual(const llcpp::fuchsia::sysmem2::PixelFormat& a,
                                   const llcpp::fuchsia::sysmem2::PixelFormat& b);
bool ImageFormatIsPixelFormatEqual(const llcpp::fuchsia::sysmem::PixelFormat& a,
                                   const llcpp::fuchsia::sysmem::PixelFormat& b);
bool ImageFormatIsPixelFormatEqual(const fuchsia_sysmem_PixelFormat& a,
                                   const fuchsia_sysmem_PixelFormat& b);

// true - The color_space is potentially compatible with the PixelFormat,
// assuming the correct variant of the ColorSpace is used (with correct bpp).
//
// false - The pixel_format bpp is not supported by the given ColorSpace.  For
// example BT.2100 specifies 10 or 12 bpp, while NV12 specifies 8 bpp, so NV12
// is not compatible with BT.2100.  Or, the system does not support the
// combination of ColorSpace and PixelFormat (even if they are hypothetically
// compatible; in this case support might be added later).
bool ImageFormatIsSupportedColorSpaceForPixelFormat(
    const llcpp::fuchsia::sysmem2::ColorSpace& color_space,
    const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format);
bool ImageFormatIsSupportedColorSpaceForPixelFormat(
    const llcpp::fuchsia::sysmem::ColorSpace& color_space,
    const llcpp::fuchsia::sysmem::PixelFormat& pixel_format);
bool ImageFormatIsSupportedColorSpaceForPixelFormat(const fuchsia_sysmem_ColorSpace& color_space,
                                                    const fuchsia_sysmem_PixelFormat& pixel_format);

// If this returns true, the remainder of the functions in this header can be
// called with pixel_format.  If this returns false, calling any other method of
// this header file may abort() and/or return a meaningless value.
bool ImageFormatIsSupported(const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format);
bool ImageFormatIsSupported(const llcpp::fuchsia::sysmem::PixelFormat& pixel_format);
bool ImageFormatIsSupported(const fuchsia_sysmem_PixelFormat* pixel_format);

// Returns the number of bits per pixel for the given PixelFormat.  This is the
// bits per pixel (RGB pixel or Y pixel) in the overall image across all bytes
// that contain pixel data.
//
// For example, NV12 is 12 bits per pixel.  This accounts for sub-sampling in
// both horizontal and vertical.
uint32_t ImageFormatBitsPerPixel(const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format);
uint32_t ImageFormatBitsPerPixel(const llcpp::fuchsia::sysmem::PixelFormat& pixel_format);
uint32_t ImageFormatBitsPerPixel(const fuchsia_sysmem_PixelFormat* pixel_format);

// This is the number of stride bytes per pixel of width (RGB pixel width or Y
// pixel width) of plane 0.  For formats such as NV12 that separate Y and UV
// data, this is the number of stride bytes of the Y plane (plane 0).  NV12 has
// the same stride for the UV data.  This function doesn't return stride
// information for planes beyond plane 0.
uint32_t ImageFormatStrideBytesPerWidthPixel(
    const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format);
uint32_t ImageFormatStrideBytesPerWidthPixel(
    const llcpp::fuchsia::sysmem::PixelFormat& pixel_format);
uint32_t ImageFormatStrideBytesPerWidthPixel(const fuchsia_sysmem_PixelFormat* pixel_format);

// This would be height * stride, if it weren't for formats like NV12, where it
// isn't.  The return value is in bytes.
uint64_t ImageFormatImageSize(const llcpp::fuchsia::sysmem2::ImageFormat& image_format);
uint64_t ImageFormatImageSize(const llcpp::fuchsia::sysmem::ImageFormat_2& image_format);
uint64_t ImageFormatImageSize(const fuchsia_sysmem_ImageFormat_2* image_format);

uint32_t ImageFormatCodedWidthMinDivisor(const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format);
uint32_t ImageFormatCodedWidthMinDivisor(const llcpp::fuchsia::sysmem::PixelFormat& pixel_format);
uint32_t ImageFormatCodedWidthMinDivisor(const fuchsia_sysmem_PixelFormat* pixel_format);

uint32_t ImageFormatCodedHeightMinDivisor(const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format);
uint32_t ImageFormatCodedHeightMinDivisor(const llcpp::fuchsia::sysmem::PixelFormat& pixel_format);
uint32_t ImageFormatCodedHeightMinDivisor(const fuchsia_sysmem_PixelFormat* pixel_format);

// The start of image data must be at least this aligned.
//
// The plane 0 stride is required to be divisible by this alignment.  Generally
// the stride of planes beyond plane 0 (if any) will have a known fixed
// relationship with the plane 0 stride.
uint32_t ImageFormatSampleAlignment(const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format);
uint32_t ImageFormatSampleAlignment(const llcpp::fuchsia::sysmem::PixelFormat& pixel_format);
uint32_t ImageFormatSampleAlignment(const fuchsia_sysmem_PixelFormat* pixel_format);

// Gets the minimum number of bytes per row possible for an image with a
// specific width and specific constraints. Returns false if the width would not
// be valid.
bool ImageFormatMinimumRowBytes(const llcpp::fuchsia::sysmem2::ImageFormatConstraints& constraints,
                                uint32_t width, uint32_t* minimum_row_bytes_out);
bool ImageFormatMinimumRowBytes(const llcpp::fuchsia::sysmem::ImageFormatConstraints& constraints,
                                uint32_t width, uint32_t* minimum_row_bytes_out);
bool ImageFormatMinimumRowBytes(const fuchsia_sysmem_ImageFormatConstraints* constraints,
                                uint32_t width, uint32_t* minimum_row_bytes_out);

bool ImageFormatConvertSysmemToZx(const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format,
                                  zx_pixel_format_t* zx_pixel_format_out);
bool ImageFormatConvertSysmemToZx(const llcpp::fuchsia::sysmem::PixelFormat& pixel_format,
                                  zx_pixel_format_t* zx_pixel_format_out);
bool ImageFormatConvertSysmemToZx(const fuchsia_sysmem_PixelFormat* pixel_format,
                                  zx_pixel_format_t* zx_pixel_format_out);

fit::result<llcpp::fuchsia::sysmem2::PixelFormat::Builder> ImageFormatConvertZxToSysmem_v2(
    fidl::Allocator* allocator, zx_pixel_format_t zx_pixel_format);
fit::result<llcpp::fuchsia::sysmem::PixelFormat> ImageFormatConvertZxToSysmem_v1(
    fidl::Allocator* allocator, zx_pixel_format_t zx_pixel_format);
bool ImageFormatConvertZxToSysmem(zx_pixel_format_t zx_pixel_format,
                                  fuchsia_sysmem_PixelFormat* pixel_format_out);

fit::result<llcpp::fuchsia::sysmem2::ImageFormat::Builder> ImageConstraintsToFormat(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::ImageFormatConstraints& constraints,
    uint32_t width, uint32_t height);
fit::result<llcpp::fuchsia::sysmem::ImageFormat_2> ImageConstraintsToFormat(
    const llcpp::fuchsia::sysmem::ImageFormatConstraints& constraints, uint32_t width,
    uint32_t height);
bool ImageConstraintsToFormat(const fuchsia_sysmem_ImageFormatConstraints* constraints,
                              uint32_t width, uint32_t height,
                              fuchsia_sysmem_ImageFormat_2* image_format_out);

bool ImageFormatPlaneByteOffset(const llcpp::fuchsia::sysmem2::ImageFormat& image_format,
                                uint32_t plane, uint64_t* offset_out);
bool ImageFormatPlaneByteOffset(const llcpp::fuchsia::sysmem::ImageFormat_2& image_format,
                                uint32_t plane, uint64_t* offset_out);
bool ImageFormatPlaneByteOffset(const fuchsia_sysmem_ImageFormat_2* image_format, uint32_t plane,
                                uint64_t* offset_out);

bool ImageFormatPlaneRowBytes(const llcpp::fuchsia::sysmem2::ImageFormat& image_format,
                              uint32_t plane, uint32_t* row_bytes_out);
bool ImageFormatPlaneRowBytes(const llcpp::fuchsia::sysmem::ImageFormat_2& image_format,
                              uint32_t plane, uint32_t* row_bytes_out);
bool ImageFormatPlaneRowBytes(const fuchsia_sysmem_ImageFormat_2* image_format, uint32_t plane,
                              uint32_t* row_bytes);

bool ImageFormatCompatibleWithProtectedMemory(
    const llcpp::fuchsia::sysmem2::PixelFormat& pixel_format);
bool ImageFormatCompatibleWithProtectedMemory(
    const llcpp::fuchsia::sysmem::PixelFormat& pixel_format);
bool ImageFormatCompatibleWithProtectedMemory(const fuchsia_sysmem_PixelFormat* pixel_format);

#endif  // LIB_IMAGE_FORMAT_IMAGE_FORMAT_H_
