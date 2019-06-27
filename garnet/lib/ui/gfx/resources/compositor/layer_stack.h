// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_LAYER_STACK_H_
#define GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_LAYER_STACK_H_

#include <unordered_set>

#include "garnet/lib/ui/gfx/engine/hit.h"
#include "garnet/lib/ui/gfx/engine/hit_tester.h"
#include "garnet/lib/ui/gfx/resources/resource.h"

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

  LayerStack(Session* session, ResourceId id);

  ~LayerStack() override;

  // Performs a hit test on all the layers in this stack, along the provided ray
  // in the layer stack's coordinate system.
  //
  // The hit collection behavior depends on the hit tester.
  std::vector<Hit> HitTest(const escher::ray4& ray, HitTester* hit_tester) const;

  // AddLayerCmd.
  bool AddLayer(LayerPtr layer);
  // RemoveLayerCmd.
  bool RemoveLayer(LayerPtr layer);
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

#endif  // GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_LAYER_STACK_H_
