// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_PIPE_BASE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_PIPE_BASE_H_

#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/image_base.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"

namespace escher {
class ReleaseFenceSignaller;
}

namespace scenic_impl::gfx {

struct ImagePipeUpdateResults {
  bool image_updated;
  std::deque<PresentImageCallback> callbacks;
};

// Abstract superclass for ImagePipe and ImagePipe2.
// TODO(35636): This class is to be DEPRECATED in favor of ImagePipe2.
class ImagePipeBase : public ImageBase {
 public:
  // Update to use the most current frame for the specified presentation time.
  // Called before rendering a frame using this ImagePipe. Returns
  // |image_updated| as true if the current Image changed since the last time
  // Update() was called, and false otherwise. |callbacks| is the list of
  // callbacks passed into |ImagePipe.PresentImage()|.
  //
  // |release_fence_signaller| is a dependency required for signalling
  // release fences correctly, since it has knowledge of when command buffers
  // are released. Cannot be null.
  //
  // This method is idempotent when called multiple times for the same |presentation_time|,
  // assuming that there are no intervening calls to PresentImage() with an earlier target
  // time.
  virtual ImagePipeUpdateResults Update(escher::ReleaseFenceSignaller* release_fence_signaller,
                                        zx::time presentation_time) = 0;

  // Resource implementation
  void Accept(class ResourceVisitor* visitor) override;

 protected:
  ImagePipeBase(Session* session, ResourceId id, const ResourceTypeInfo& type_info);
};

}  // namespace scenic_impl::gfx

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_PIPE_BASE_H_
