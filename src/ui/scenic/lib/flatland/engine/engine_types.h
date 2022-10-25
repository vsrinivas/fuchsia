// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_ENGINE_TYPES_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_ENGINE_TYPES_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <zircon/pixelformat.h>

#include <functional>

#include "src/ui/scenic/lib/flatland/flatland_types.h"
#include "src/ui/scenic/lib/flatland/link_system.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"

namespace flatland {

// Struct to represent the display's flatland info. The TransformHandle must be the root
// transform of the root Flatland instance. A new DisplayInfo struct is added to the
// display_map_ when a client calls AddDisplay().
struct DisplayInfo {
  // The width and height of the display in pixels.
  glm::uvec2 dimensions;

  // The pixel formats available on this particular display.
  std::vector<zx_pixel_format_t> formats;
};

// The data that gets forwarded either to the display or the software renderer. The lengths
// of |rectangles| and |images| must be the same, and each rectangle/image pair for a given
// index represents a single renderable object.
struct RenderData {
  std::vector<ImageRect> rectangles;
  std::vector<allocation::ImageMetadata> images;
  // TODO(fxbug.dev/70464): should we remove this, and pass to RenderFrame() as a std::map of
  // RenderData keyed by display_id?  That would have the benefit of guaranteeing by construction
  // that each display_id could only appear once.
  uint64_t display_id;
};

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
  static DisplaySrcDstFrames New(ImageRect rectangle, allocation::ImageMetadata image);
};

// Options for BufferCollectionImporter usage modes for DisplayCompositor.
enum class BufferCollectionImportMode {
  // Display constraints are treated as must-have. We fail allocating sysmem backed buffers
  // if display and renderer constraints result in unsuccessful allocation.
  // Note that renderer constraints are still applied. There are some formats which aren't widely
  // supported by displays, i.e. YUV formats, that might not be allocated.
  EnforceDisplayConstraints,
  // Display constraints are treated as optional. They are added as AttachTokens and we check if
  // they happened to work. Note that renderer constraints are still applied.
  AttemptDisplayConstraints,
  // Display constraints are skipped and assumed as failed. Note that renderer constraints are still
  // applied.
  RendererOnly
};

BufferCollectionImportMode StringToBufferCollectionImportMode(const std::string& str);

const char* StringFromBufferCollectionImportMode(BufferCollectionImportMode mode);

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_ENGINE_TYPES_H_
