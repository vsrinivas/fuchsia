// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_COMPOSITOR_H_
#define GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_COMPOSITOR_H_

#include "garnet/lib/ui/gfx/resources/resource.h"

#include <lib/zx/time.h>
#include <set>
#include <utility>

#include "garnet/lib/ui/gfx/swapchain/swapchain.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace scenic_impl {
namespace gfx {

class Compositor;
class FrameTimings;
class Layer;
class LayerStack;
class Scene;
class Swapchain;
using CompositorPtr = fxl::RefPtr<Compositor>;
using FrameTimingsPtr = fxl::RefPtr<FrameTimings>;
using LayerStackPtr = fxl::RefPtr<LayerStack>;

// A Compositor composes multiple layers into a single image.  This is intended
// to provide an abstraction that can make use of hardware overlay layers.
class Compositor : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // TODO(SCN-452): there is currently no way to create/attach a display, so
  // this compositor will never render anything.
  static CompositorPtr New(Session* session, ResourceId id);

  ~Compositor() override;

  // SetLayerStackCmd.
  bool SetLayerStack(LayerStackPtr layer_stack);
  const LayerStackPtr& layer_stack() const { return layer_stack_; }

  // Add scenes in all layers to |scenes_out|.
  void CollectScenes(std::set<Scene*>* scenes_out);

  std::pair<uint32_t, uint32_t> GetBottomLayerSize() const;
  int GetNumDrawableLayers() const;

  // | Resource |
  void Accept(class ResourceVisitor* visitor) override;

  // Returns the list of drawable layers from the layer stack.
  std::vector<Layer*> GetDrawableLayers() const;

  Swapchain* swapchain() const { return swapchain_.get(); }

 protected:
  Compositor(Session* session, ResourceId id, const ResourceTypeInfo& type_info,
             std::unique_ptr<Swapchain> swapchain);

 private:
  std::unique_ptr<Swapchain> swapchain_;
  LayerStackPtr layer_stack_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Compositor);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_COMPOSITOR_H_
