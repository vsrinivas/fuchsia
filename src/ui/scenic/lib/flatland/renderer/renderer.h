// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_RENDERER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_RENDERER_H_

#include <lib/zx/event.h>

#include <optional>

#include "src/ui/scenic/lib/flatland/buffers/buffer_collection_importer.h"

// clang-format off
#include "src/ui/lib/glm_workaround/glm_workaround.h"
// clang-format on

#include <glm/mat3x3.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include "src/ui/lib/escher/geometry/types.h"

namespace flatland {

// Typedef to a flatland type.
using Rectangle2D = escher::Rectangle2D;

// Contains information regarding the constraints of a particular buffer collection. This
// should be used by the Flatland instance to check that images it wishes to create are
// valid. More information may be added to this struct over time.
struct BufferCollectionMetadata {
  // The number of vmos available in the buffer collection.
  uint32_t vmo_count;

  // Various image constraint information, including max width/height and supported
  // pixel formats. See |constraints.fidl| for more information.
  fuchsia::sysmem::ImageFormatConstraints image_constraints;
};

// This is the main renderer interface used by the Flatland System. Since Flatland is
// agnostic to the implementation of the renderer, it is declared here as a virtual
// interface, whose concrete implementation is to be injected into Flatland.
class Renderer : public BufferCollectionImporter {
 public:
  // This function is for registering collections that contain render targets. In order for an image
  // to be used as a render target in the Render() function below, the buffer collection it
  // is associated with must have been registered with this function.
  //
  // This function is likewise threadsafe, although it is only meant to be called from the render
  // loop, and not by any flatland instance directly.
  virtual bool RegisterRenderTargetCollection(
      sysmem_util::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) = 0;

  // Removes a buffer collection used for render targets from the renderer. Once done, the
  // collection_id can be reused for another buffer collection.
  virtual void DeregisterRenderTargetCollection(
      sysmem_util::GlobalBufferCollectionId collection_id) = 0;

  // This function validates if the buffer collection referenced by |collection_id| is ready to
  // be used in rendering operations and must be called before that buffer collection is used
  // in |Render|. Specifically, this function checks to make sure that the buffer collection is
  // fully allocated (which can only happen after all token holders have set constraints and the
  // client has requested for sysmem to allocate the buffer collection via a call to
  // WaitForBuffersAllocated() or an equivalent function).
  //
  // If the collection has not yet been fully allocated, or if the collection has not been
  // registered with the renderer via a call to RegisterBufferCollection(), this function will
  // return std::nullopt. If the collection has been registered and has been fully allocated, then
  // this function will return a BufferCollectionMetadata struct containing information pertaining
  // to the number of vmos as well as image constraints, such as maximum width/height (see
  // BufferCollectionMetadata at the top of this file for more information).
  //
  // The BufferCollectionMetadata can then be used by the Flatland instance to verify images it
  // wishes to create with this buffer collection. Images map N:1 with buffer collections, since a
  // collection can consist of multiple VMOs, and each VMO in turn can back multiple images. The
  // intended usage then is for the Flatland instance to store the BufferCollectionMetadata it
  // receives and use it to compare against any incoming images. For example, if a buffer collection
  // only has 5 VMOs and an image is referencing vmo index 6, then that would be an error.
  //
  // This function needs to be separate from RegisterBufferCollection() since it is too early to
  // return a |BufferCollectionMetadata| struct at registration time., as the client may have not
  // yet set their constraints, or duplicated their token. Combining this function with the above
  // registration function would require us to block on registration, which could go on forever.
  //
  // Lastly, this function needs to communicate with sysmem and may take some time. However, it does
  // not block indefinitely. Additionally, it only needs to be called once per buffer collection if
  // the call was successful. It is up to the Flatland instance how to proceed in the event of
  // failure. Calling this function multiple times until it returns true is allowed.
  virtual std::optional<BufferCollectionMetadata> Validate(
      sysmem_util::GlobalBufferCollectionId collection_id) = 0;

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
  virtual void Render(const ImageMetadata& render_target,
                      const std::vector<Rectangle2D>& rectangles,
                      const std::vector<ImageMetadata>& images,
                      const std::vector<zx::event>& release_fences = {}) = 0;

  virtual ~Renderer() = default;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_RENDERER_H_
