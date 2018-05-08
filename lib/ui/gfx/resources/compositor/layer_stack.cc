// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"

#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"

namespace scenic {
namespace gfx {

const ResourceTypeInfo LayerStack::kTypeInfo = {ResourceType::kLayerStack,
                                                "LayerStack"};

LayerStack::LayerStack(Session* session, scenic::ResourceId id)
    : Resource(session, id, LayerStack::kTypeInfo) {}

LayerStack::~LayerStack() = default;

std::vector<Hit> LayerStack::HitTest(const escher::ray4& ray,
                                     Session* session) const {
  std::vector<Hit> hits;
  for (auto layer : layers_) {
    std::vector<Hit> layer_hits = layer->HitTest(ray, session);
    hits.insert(hits.end(), layer_hits.begin(), layer_hits.end());
  }
  return hits;
}

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

bool LayerStack::RemoveLayer(LayerPtr layer) {
  if (layer->layer_stack_ != this) {
    error_reporter()->ERROR()
        << "LayerStack::RemoveLayer(): layer doesn't belong to this stack.";
    return false;
  }
  layer->layer_stack_ = nullptr;
  layers_.erase(layer);
  return true;
}

bool LayerStack::RemoveAllLayers() {
  for (const auto& layer : layers_) {
    layer->layer_stack_ = nullptr;
  }
  layers_.clear();
  return true;
}

void LayerStack::RemoveLayer(Layer* layer) {
  auto it = std::find_if(
      layers_.begin(), layers_.end(),
      [layer](const LayerPtr& layer_ptr) { return layer == layer_ptr.get(); });
  FXL_DCHECK(it != layers_.end());
  layers_.erase(it);
  (*it)->layer_stack_ = nullptr;
}

}  // namespace gfx
}  // namespace scenic
