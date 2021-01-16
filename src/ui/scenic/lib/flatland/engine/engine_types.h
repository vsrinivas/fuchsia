// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_TYPES_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_TYPES_H_

#include <fuchsia/hardware/display/cpp/fidl.h>

#include <functional>

#include "src/ui/scenic/lib/flatland/link_system.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"

namespace flatland {

// Struct to represent the display's flatland info. The TransformHandle must be the root
// transform of the root Flatland instance. The pixel scale is the ratio between the
// display's width and height and the logical pixel size calculated from the provided
// transform handle. A new DisplayInfo struct is added to the display_map_ when a client
// calls AddDisplay().
struct DisplayInfo {
  TransformHandle transform;
  glm::uvec2 pixel_scale;
};

// The data that gets forwarded either to the display or the software renderer. The lengths
// of |rectangles| and |images| must be the same, and each rectangle/image pair for a given
// index represents a single renderable object.
struct RenderData {
  std::vector<Rectangle2D> rectangles;
  std::vector<ImageMetadata> images;
  uint64_t display_id;
};

// This function is used by the engine to extract render data. By making a function pointer that
// we can pass directly to the engine, we can curate data by hand for the engine to use without
// even having to touch any flatland code.
using RenderDataFunc =
    std::function<std::vector<RenderData>(std::unordered_map<uint64_t, DisplayInfo>)>;

// Struct to combine the source and destination frames used to set a layer's
// position on the display. The src frame represents the (cropped) UV coordinates
// of the image and the dst frame represents the position in screen space that
// the layer will be placed.
struct DisplaySrcDstFrames {
  fuchsia::hardware::display::Frame src;
  fuchsia::hardware::display::Frame dst;

  // When setting an image on a layer in the display, you have to specify the "source"
  // and "destination", where the source represents the pixel offsets and dimensions to
  // use from the image and the destination represents where on the display the (cropped)
  // image will go in pixel coordinates. This exactly mirrors the setup we have in the
  // Rectangle2D struct and ImageMetadata struct, so we just need to convert that over to
  // the proper display controller readable format. The input rectangle contains both the
  // source and destination information.
  static DisplaySrcDstFrames New(escher::Rectangle2D rectangle, ImageMetadata image);
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_TYPES_H_
