// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_LAYER_STACK_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_LAYER_STACK_H_

#include <memory>
#include <unordered_set>

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
