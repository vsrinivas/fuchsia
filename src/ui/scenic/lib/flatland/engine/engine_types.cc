// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/engine_types.h"

namespace flatland {

DisplaySrcDstFrames DisplaySrcDstFrames::New(escher::Rectangle2D rectangle,
                                             allocation::ImageMetadata image) {
  // TODO(fxbug.dev/77993): This will not produce the correct results for the display
  // controller rendering pathway if a rotation has been applied to the rectangle already.
  // Please see comment with same bug number in display_compositor.cc for more details.
  fuchsia::hardware::display::Frame src_frame = {
      .x_pos = static_cast<uint32_t>(rectangle.clockwise_uvs[0].x * image.width),
      .y_pos = static_cast<uint32_t>(rectangle.clockwise_uvs[0].y * image.height),
      .width = static_cast<uint32_t>((rectangle.clockwise_uvs[2].x - rectangle.clockwise_uvs[0].x) *
                                     image.width),
      .height = static_cast<uint32_t>(
          (rectangle.clockwise_uvs[2].y - rectangle.clockwise_uvs[0].y) * image.height),
  };

  fuchsia::hardware::display::Frame dst_frame = {
      .x_pos = static_cast<uint32_t>(rectangle.origin.x),
      .y_pos = static_cast<uint32_t>(rectangle.origin.y),
      .width = static_cast<uint32_t>(rectangle.extent.x),
      .height = static_cast<uint32_t>(rectangle.extent.y),
  };
  return {.src = src_frame, .dst = dst_frame};
}

}  // namespace flatland
