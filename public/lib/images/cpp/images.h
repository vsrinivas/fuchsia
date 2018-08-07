// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_IMAGES_CPP_IMAGES_H_
#define LIB_IMAGES_CPP_IMAGES_H_

#include <fuchsia/images/cpp/fidl.h>

#include <stdint.h>

namespace images {

// Returns the number of bits per pixel for the given format.
size_t BitsPerPixel(const fuchsia::images::PixelFormat& pixel_format);

// Returns the pixel alignment for the given format.
//
// This is technically something closer to "max sample alignment" for the given
// format.  For example, NV12 returns 2 here because its UV interleaved data has
// 2 bytes per sample, despite its Y plane having 1 byte per sample.
//
// The stride is required to be divisible by this alignment.
size_t MaxSampleAlignment(const fuchsia::images::PixelFormat& pixel_format);

// This would be height * stride, if it weren't for formats like NV12, where it
// isn't.  The output is bytes.
size_t ImageSize(const fuchsia::images::ImageInfo& image_info);

}  // namespace images

#endif  // LIB_IMAGES_CPP_IMAGES_H_
