// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_SIMPLEST_SYSMEM_PNG_HELPER_H_
#define SRC_UI_EXAMPLES_SIMPLEST_SYSMEM_PNG_HELPER_H_

#include <stdint.h>

namespace png_helper {

struct PNGImageSize {
  uint32_t width;
  uint32_t height;
};

// Load png file from resources. Image data will be loaded to `out_bytes`.
//
// @param size Output width and height of the png file.
// @param out_bytes Output bytes of the png file.
void LoadPngFromFile(PNGImageSize* size, uint8_t** out_bytes);

// Right now we only have this one png file. Consider making this a component arg when we want to
// support more png files.
static constexpr char kSmileyPath[] = "/pkg/data/images/smiley.png";

static constexpr uint8_t kPNGHeaderBytes = 8;

}  // namespace png_helper
#endif  // SRC_UI_EXAMPLES_SIMPLEST_SYSMEM_PNG_HELPER_H_
