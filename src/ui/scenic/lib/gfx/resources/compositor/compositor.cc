// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"

#include "src/ui/lib/escher/renderer/semaphore.h"
#include "src/ui/scenic/lib/gfx/engine/scene_graph.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/swapchain/swapchain.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Compositor::kTypeInfo = {ResourceType::kCompositor, "Compositor"};
const CompositorWeakPtr Compositor::kNullWeakPtr = CompositorWeakPtr();

CompositorPtr Compositor::New(Session* session, SessionId session_id, ResourceId id,
                              SceneGraphWeakPtr scene_graph) {
  return fxl::AdoptRef(
      new Compositor(session, session_id, id, Compositor::kTypeInfo, scene_graph, nullptr));
}

Compositor::Compositor(Session* session, SessionId session_id, ResourceId id,
                       const ResourceTypeInfo& type_info, SceneGraphWeakPtr scene_graph,
                       std::unique_ptr<Swapchain> swapchain)
    : Resource(session, session_id, id, type_info),
      scene_graph_(scene_graph),
      swapchain_(std::move(swapchain)),
      weak_factory_(this) {
  FX_DCHECK(scene_graph_);
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
  const Layer* layer = GetDrawableLayer();
  FX_CHECK(layer) << "No drawable layers";
  return {layer->width(), layer->height()};
}

Layer* Compositor::GetDrawableLayer() const {
  if (!layer_stack_ || layer_stack_->layers().empty()) {
    return nullptr;
  }
  FX_DCHECK(layer_stack_->layers().size() == 1);
  Layer* layer = layer_stack_->layers().begin()->get();
  return layer->IsDrawable() ? layer : nullptr;
}

// Rotation values can only be multiples of 90 degrees. Logs an
// error and returns false, without setting the rotation, if this
// condition is not met.
bool Compositor::SetLayoutRotation(uint32_t rotation, ErrorReporter* error_reporter) {
  if (rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270) {
    layout_rotation_ = rotation;
    return true;
  }

  error_reporter->ERROR()
      << "Compositor::SetLayoutRotation() rotation must be 0, 90, 180, or 270 degrees";
  return false;
}

}  // namespace gfx
}  // namespace scenic_impl
