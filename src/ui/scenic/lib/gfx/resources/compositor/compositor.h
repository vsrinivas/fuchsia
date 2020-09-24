// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_COMPOSITOR_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_COMPOSITOR_H_

#include <lib/zx/time.h>

#include <set>
#include <utility>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"
#include "src/ui/scenic/lib/gfx/swapchain/swapchain.h"

namespace scenic_impl {
namespace gfx {

class Compositor;
class SceneGraph;
class Layer;
class LayerStack;
class Scene;
class Swapchain;
using CompositorPtr = fxl::RefPtr<Compositor>;
using CompositorWeakPtr = fxl::WeakPtr<Compositor>;
using LayerStackPtr = fxl::RefPtr<LayerStack>;
using SceneGraphWeakPtr = fxl::WeakPtr<SceneGraph>;

// A Compositor composes multiple layers into a single image.  This is
// intended to provide an abstraction that can make use of hardware overlay
// layers.
class Compositor : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  static const CompositorWeakPtr kNullWeakPtr;

  // TODO(fxbug.dev/23686): there is currently no way to create/attach a display, so
  // this compositor will never render anything.
  static CompositorPtr New(Session* session, SessionId session_id, ResourceId id,
                           SceneGraphWeakPtr scene_graph);

  ~Compositor() override;

  // SetLayerStackCmd.
  bool SetLayerStack(LayerStackPtr layer_stack);
  const LayerStackPtr& layer_stack() const { return layer_stack_; }

  CompositorWeakPtr GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Add scenes in all layers to |scenes_out|.
  void CollectScenes(std::set<Scene*>* scenes_out);

  std::pair<uint32_t, uint32_t> GetBottomLayerSize() const;
  int GetNumDrawableLayers() const;

  // | Resource |
  void Accept(class ResourceVisitor* visitor) override;

  // Returns the list of drawable layers from the layer stack.
  std::vector<Layer*> GetDrawableLayers() const;

  Swapchain* swapchain() const { return swapchain_.get(); }

  // Setter and getter for rotation in degrees, currently used for
  // screenshotting.
  bool SetLayoutRotation(uint32_t rotation, ErrorReporter* reporter);

  const uint32_t& layout_rotation() const { return layout_rotation_; }

 protected:
  Compositor(Session* session, SessionId session_id, ResourceId id,
             const ResourceTypeInfo& type_info, SceneGraphWeakPtr scene_graph,
             std::unique_ptr<Swapchain> swapchain);

 private:
  SceneGraphWeakPtr scene_graph_;
  std::unique_ptr<Swapchain> swapchain_;
  LayerStackPtr layer_stack_;

  // Rotation in degrees used for screenshotting.
  uint32_t layout_rotation_ = 0;

  fxl::WeakPtrFactory<Compositor> weak_factory_;  // Must be last.

  FXL_DISALLOW_COPY_AND_ASSIGN(Compositor);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_COMPOSITOR_H_
