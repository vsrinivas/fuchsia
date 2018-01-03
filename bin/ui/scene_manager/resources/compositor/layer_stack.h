// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_COMPOSITOR_LAYER_STACK_H_
#define GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_COMPOSITOR_LAYER_STACK_H_

#include "garnet/bin/ui/scene_manager/resources/resource.h"
#include "garnet/bin/ui/scene_manager/engine/hit.h"

#include <unordered_set>

namespace scene_manager {

class Layer;
class LayerStack;
using LayerPtr = fxl::RefPtr<Layer>;
using LayerStackPtr = fxl::RefPtr<LayerStack>;

// A stack of Layers that can be composited by a Compositor.
class LayerStack : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  LayerStack(Session* session, scenic::ResourceId id);

  ~LayerStack() override;

  // Performs a hit test on all the layers in this stack, along the provided ray
  // in the layer stack's coordinate system.
  //
  // |session| is a pointer to the session that initiated the hit test.
  std::vector<Hit> HitTest(const escher::ray4& ray, Session* session) const;

  // AddLayerOp.
  bool AddLayer(LayerPtr layer);
  const std::unordered_set<LayerPtr>& layers() const { return layers_; }

  // | Resource |
  void Accept(class ResourceVisitor* visitor) override;

 private:
  friend class Layer;
  void RemoveLayer(Layer* layer);

  std::unordered_set<LayerPtr> layers_;
};

}  // namespace scene_manager

#endif  // GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_COMPOSITOR_LAYER_STACK_H_
