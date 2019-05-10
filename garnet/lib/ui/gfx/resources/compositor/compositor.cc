// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"

#include "garnet/lib/ui/gfx/engine/scene_graph.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/swapchain/swapchain.h"
#include "src/ui/lib/escher/renderer/semaphore.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Compositor::kTypeInfo = {ResourceType::kCompositor,
                                                "Compositor"};
const CompositorWeakPtr Compositor::kNullWeakPtr = CompositorWeakPtr();

CompositorPtr Compositor::New(Session* session, ResourceId id,
                              SceneGraphWeakPtr scene_graph) {
  return fxl::AdoptRef(
      new Compositor(session, id, Compositor::kTypeInfo, scene_graph, nullptr));
}

Compositor::Compositor(Session* session, ResourceId id,
                       const ResourceTypeInfo& type_info,
                       SceneGraphWeakPtr scene_graph,
                       std::unique_ptr<Swapchain> swapchain)
    : Resource(session, id, type_info),
      scene_graph_(scene_graph),
      swapchain_(std::move(swapchain)),
      weak_factory_(this) {
  FXL_DCHECK(scene_graph_);
  scene_graph_->AddCompositor(GetWeakPtr());
}

Compositor::~Compositor() {
  if (scene_graph_) {
    scene_graph_->RemoveCompositor(GetWeakPtr());
  }
}

void Compositor::CollectScenes(std::set<Scene*>* scenes_out) {
  if (layer_stack_) {
    for (auto& layer : layer_stack_->layers()) {
      layer->CollectScenes(scenes_out);
    }
  }
}

bool Compositor::SetLayerStack(LayerStackPtr layer_stack) {
  layer_stack_ = std::move(layer_stack);
  return true;
}

std::pair<uint32_t, uint32_t> Compositor::GetBottomLayerSize() const {
  std::vector<Layer*> drawable_layers = GetDrawableLayers();
  FXL_CHECK(!drawable_layers.empty()) << "No drawable layers";
  return {drawable_layers[0]->width(), drawable_layers[0]->height()};
}

int Compositor::GetNumDrawableLayers() const {
  return GetDrawableLayers().size();
}

std::vector<Layer*> Compositor::GetDrawableLayers() const {
  if (!layer_stack_) {
    return std::vector<Layer*>();
  }
  std::vector<Layer*> drawable_layers;
  for (auto& layer : layer_stack_->layers()) {
    if (layer->IsDrawable()) {
      drawable_layers.push_back(layer.get());
    }
  }
  // Sort the layers from bottom to top.
  std::sort(drawable_layers.begin(), drawable_layers.end(), [](auto a, auto b) {
    return a->translation().z < b->translation().z;
  });

  return drawable_layers;
}

// Rotation values can only be multiples of 90 degrees. Logs an
// error and returns false, without setting the rotation, if this
// condition is not met.
bool Compositor::SetLayoutRotation(uint32_t rotation) {
  if (rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270) {
    layout_rotation_ = rotation;
    return true;
  }

  session()->error_reporter()->ERROR()
      << "Compositor::SetLayoutRotation() rotation must be 0, 90, 180, or 270 "
         "degrees";
  return false;
}

}  // namespace gfx
}  // namespace scenic_impl
