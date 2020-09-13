// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_H_

#include "src/ui/scenic/lib/display/util.h"
#include "src/ui/scenic/lib/flatland/link_system.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"

namespace flatland {

class Engine {
 public:
  // Pair of flatland renderer and display controller ids that both
  // point to the same buffer collection.
  struct BufferCollectionIdPair {
    GlobalBufferCollectionId global_id;
    scenic_impl::DisplayBufferCollectionId display_id;
  };

  Engine(const std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr>& display_controller,
         const std::shared_ptr<Renderer>& renderer, const std::shared_ptr<LinkSystem>& link_system,
         const std::shared_ptr<UberStructSystem>& uber_struct_system);

  // TODO(fxbug.dev/59646): Add in parameters for scheduling, etc. Right now we're just making sure
  // the data is processed correctly.
  void RenderFrame();

  // Register a new display to the engine. The display_id is a unique display to reference the
  // display object by, and can be retrieved by calling display_id() on a display object. The
  // TransformHandle must be the root transform of the root Flatland instance. The pixel scale is
  // the display's width/height.
  // TODO(fxbug.dev/59646): We need to figure out exactly how we want the display to anchor
  // to the Flatland hierarchy.
  void AddDisplay(uint64_t display_id, TransformHandle transform, glm::uvec2 pixel_scale);

  // Registers a sysmem buffer collection with the engine, causing it to register with both
  // the display controller and the renderer. A valid display must have already been added
  // to the Engine via |AddDisplay| before this is called with the same display_id.
  // The result is a BufferCollectionIdPair which contains both global and display ids for the
  // buffer collection. If the collection failed to allocate, both ids will be invalid (0).
  BufferCollectionIdPair RegisterTargetCollection(fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                                                  uint64_t display_id, uint32_t num_vmos);

 private:
  // The data that gets forwarded either to the display or the software renderer. The lengths
  // of |rectangles| and |images| must be the same, and each rectangle/image pair for a given
  // index represents a single renderable object.
  struct RenderData {
    std::vector<Rectangle2D> rectangles;
    std::vector<ImageMetadata> images;
    uint64_t display_id;
  };

  // Struct to represent the display's flatland info. The TransformHandle must be the root
  // transform of the root Flatland instance. The pixel scale is the display's width/height.
  // A new DisplayInfo struct is added to the display_map_ when a client calls AddDisplay().
  struct DisplayInfo {
    TransformHandle transform;
    glm::uvec2 pixel_scale;
  };

  // Gathers all of the flatland data and converts it all into a format that can be
  // directly converted into the data required by the display and the 2D renderer.
  // This is done per-display, so the result is a vector of per-display render data.
  std::vector<RenderData> ComputeRenderData();

  // The display controller is needed to create layers, import images, etc to the
  // display hardware, to bypass rendering in software when applicable.
  std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller_;

  // Software renderer used when render data cannot be directly composited to the display.
  std::shared_ptr<Renderer> renderer_;

  // The link system and uberstruct system are used to extract flatland render data.
  std::shared_ptr<LinkSystem> link_system_;
  std::shared_ptr<UberStructSystem> uber_struct_system_;

  // Maps display unique ids to the displays' flatland specific data.
  std::unordered_map<uint64_t, DisplayInfo> display_map_;

  // This map is for mapping a display ID to a pair of BufferCollection IDs referencing the
  // same buffer collection (one for the software renderer and one for the display) that
  // are configured to be compatible with that display.
  std::unordered_map<uint64_t, BufferCollectionIdPair> framebuffer_id_map_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_H_
