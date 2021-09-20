// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_RENDERER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_RENDERER_H_

#include <lib/zx/event.h>

#include <optional>

#include "fuchsia/math/cpp/fidl.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"

// clang-format off
#include "src/ui/lib/glm_workaround/glm_workaround.h"
// clang-format on

#include <zircon/pixelformat.h>

#include <glm/mat3x3.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace flatland {

// Typedef to a flatland type.
using Rectangle2D = escher::Rectangle2D;

// This is the main renderer interface used by the Flatland System. Since Flatland is
// agnostic to the implementation of the renderer, it is declared here as a virtual
// interface, whose concrete implementation is to be injected into Flatland.
class Renderer : public allocation::BufferCollectionImporter {
 public:
  // This function is for registering collections that contain render targets. In order for an image
  // to be used as a render target in the Render() function below, the buffer collection it
  // is associated with must have been registered with this function.
  //
  // This function is likewise threadsafe, although it is only meant to be called from the render
  // loop, and not by any flatland instance directly.
  //
  // |size| may be optionally set to indicate the intended size usage so that it may be specified
  // when setting constraints in |token|.
  virtual bool RegisterRenderTargetCollection(
      allocation::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
      fuchsia::math::SizeU size = {}) = 0;

  // Removes a buffer collection used for render targets from the renderer. Once done, the
  // collection_id can be reused for another buffer collection.
  virtual void DeregisterRenderTargetCollection(
      allocation::GlobalBufferCollectionId collection_id) = 0;

  // This function is responsible for rendering a single batch of Flatland rectangles into a
  // render target. This function is designed to be called on the render thread, not on any
  // Flatland instance thread. The specific behavior may differ depending on the specific subclass
  // implementation, which may also be a null renderer.
  //
  // This function assumes that clients have already validated their input data by comparing it
  // against the BufferCollectionMetadata received from calling |Validate| above. This function
  // will abort if invalid data has been passed in or if |Validate| has never been called.
  //
  // The size of the arrays |rectangles| and |images| must match or else this function will CHECK.
  // Entries in each array with the same index will be used together in rendering.
  //
  // The vector of release fences will be signaled once rendering has completed. Clients can use
  // these fences to coordinate with other work that needs to wait until rendering is completed
  // to be executed.
  virtual void Render(const allocation::ImageMetadata& render_target,
                      const std::vector<Rectangle2D>& rectangles,
                      const std::vector<allocation::ImageMetadata>& images,
                      const std::vector<zx::event>& release_fences = {}) = 0;

  // Returns the pixel format that the renderer prefers to use for render targets.
  // TODO(fxbug.dev/71410): Remove all references to zx_pixel_format_t
  virtual zx_pixel_format_t ChoosePreferredPixelFormat(
      const std::vector<zx_pixel_format_t>& available_formats) const = 0;

  virtual ~Renderer() = default;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_RENDERER_H_
