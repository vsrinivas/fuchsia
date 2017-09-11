// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/resources/compositor/layer_stack.h"

#include "garnet/bin/ui/scene_manager/resources/compositor/layer.h"
#include "garnet/bin/ui/scene_manager/util/error_reporter.h"

namespace scene_manager {

const ResourceTypeInfo LayerStack::kTypeInfo = {ResourceType::kLayerStack,
                                                "LayerStack"};

LayerStack::LayerStack(Session* session, scenic::ResourceId id)
    : Resource(session, id, LayerStack::kTypeInfo) {}

LayerStack::~LayerStack() = default;

bool LayerStack::AddLayer(LayerPtr layer) {
  if (layer->layer_stack_) {
    error_reporter()->ERROR()
        << "LayerStack::AddLayer(): layer already belongs to a LayerStack.";
    return false;
  }
  layer->layer_stack_ = this;
  layers_.insert(std::move(layer));
  return true;
}

void LayerStack::RemoveLayer(Layer* layer) {
  auto it = std::find_if(
      layers_.begin(), layers_.end(),
      [layer](const LayerPtr& layer_ptr) { return layer == layer_ptr.get(); });
  FXL_DCHECK(it != layers_.end());
  layers_.erase(it);
}

}  // namespace scene_manager
