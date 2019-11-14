// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_LAYER_STACK_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_LAYER_STACK_H_

#include <memory>
#include <unordered_set>

#include "src/ui/scenic/lib/gfx/engine/hit.h"
#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"

namespace scenic_impl {
namespace gfx {

class Layer;
class LayerStack;
using LayerPtr = fxl::RefPtr<Layer>;
using LayerStackPtr = fxl::RefPtr<LayerStack>;

// A stack of Layers that can be composited by a Compositor.
class LayerStack : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  LayerStack(Session* session, SessionId session_id, ResourceId id);

  ~LayerStack() override;

  // Performs a hit test on all the layers in this stack, along the provided ray
  // in the layer stack's coordinate system.
  //
  // The hit collection behavior depends on the accumulator. These hits include transforms into view
  // space.
  void HitTest(const escher::ray4& ray, HitAccumulator<ViewHit>* hit_accumulator) const;

  // AddLayerCmd.
  bool AddLayer(LayerPtr layer, ErrorReporter* reporter);
  // RemoveLayerCmd.
  bool RemoveLayer(LayerPtr layer, ErrorReporter* reporter);
  // RemoveAllLayersCmd.
  bool RemoveAllLayers();

  const std::unordered_set<LayerPtr>& layers() const { return layers_; }

  // | Resource |
  void Accept(class ResourceVisitor* visitor) override;

 private:
  friend class Layer;
  void RemoveLayer(Layer* layer);

  std::unordered_set<LayerPtr> layers_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_LAYER_STACK_H_
