// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/engine_types.h"

namespace flatland {

DisplaySrcDstFrames DisplaySrcDstFrames::New(ImageRect rectangle, allocation::ImageMetadata image) {
  fuchsia::hardware::display::Frame src_frame = {
      .x_pos = static_cast<uint32_t>(rectangle.texel_uvs[0].x),
      .y_pos = static_cast<uint32_t>(rectangle.texel_uvs[0].y),
      .width = static_cast<uint32_t>(rectangle.texel_uvs[2].x - rectangle.texel_uvs[0].x),
      .height = static_cast<uint32_t>(rectangle.texel_uvs[2].y - rectangle.texel_uvs[0].y),
  };

  fuchsia::hardware::display::Frame dst_frame = {
      .x_pos = static_cast<uint32_t>(rectangle.origin.x),
      .y_pos = static_cast<uint32_t>(rectangle.origin.y),
      .width = static_cast<uint32_t>(rectangle.extent.x),
      .height = static_cast<uint32_t>(rectangle.extent.y),
  };
  return {.src = src_frame, .dst = dst_frame};
}

BufferCollectionImportMode StringToBufferCollectionImportMode(const std::string& str) {
  if (str == "enforce_display_constraints") {
    return BufferCollectionImportMode::EnforceDisplayConstraints;
  } else if (str == "attempt_display_constraints") {
    return BufferCollectionImportMode::AttemptDisplayConstraints;
  } else if (str == "renderer_only") {
    return BufferCollectionImportMode::RendererOnly;
  }
  FX_LOGS(ERROR) << "Received unexpected string for flatland_buffer_collection_import_mode";
  return BufferCollectionImportMode::AttemptDisplayConstraints;
}

const char* StringFromBufferCollectionImportMode(BufferCollectionImportMode mode) {
  switch (mode) {
    case BufferCollectionImportMode::EnforceDisplayConstraints:
      return "enforce_display_constraints";
    case BufferCollectionImportMode::AttemptDisplayConstraints:
      return "attempt_display_constraints";
    case BufferCollectionImportMode::RendererOnly:
      return "renderer_only";
  }
}

}  // namespace flatland
