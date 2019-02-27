// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_FEEDBACK_AGENT_IMAGE_CONVERSION_H_
#define GARNET_BIN_FEEDBACK_AGENT_IMAGE_CONVERSION_H_

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <png.h>

namespace fuchsia {
namespace feedback {

// Encodes |raw_image| in PNG.
//
// The only |pixel_format| supported today is BGRA_8.
bool RawToPng(const fuchsia::mem::Buffer& raw_image, size_t height,
              size_t width, size_t stride,
              fuchsia::images::PixelFormat pixel_format,
              fuchsia::mem::Buffer* png_image);

}  // namespace feedback
}  // namespace fuchsia

#endif  // GARNET_BIN_FEEDBACK_AGENT_IMAGE_CONVERSION_H_
