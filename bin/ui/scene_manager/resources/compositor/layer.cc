// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/resources/compositor/layer.h"

#include "garnet/bin/ui/scene_manager/resources/camera.h"
#include "garnet/bin/ui/scene_manager/resources/compositor/layer_stack.h"
#include "garnet/bin/ui/scene_manager/resources/renderers/renderer.h"
#include "garnet/bin/ui/scene_manager/util/error_reporter.h"

namespace scene_manager {

const ResourceTypeInfo Layer::kTypeInfo = {ResourceType::kLayer, "Layer"};

Layer::Layer(Session* session, scenic::ResourceId id)
    : Resource(session, id, Layer::kTypeInfo), translation_(0) {}

Layer::~Layer() = default;

bool Layer::SetRenderer(RendererPtr renderer) {
  // TODO(MZ-249): if layer content is already specified as an image, clear it
  // before setting the renderer.  Or call it an error, and require the client
  // to explicitly clear it first.
  renderer_ = std::move(renderer);
  return true;
}

bool Layer::SetSize(const escher::vec2& size) {
  if (size.x <= 0 || size.y <= 0) {
    if (size != escher::vec2(0, 0)) {
      error_reporter()->ERROR()
          << "scene_manager::Layer::SetSize(): size must be positive";
      return false;
    }
  }
  size_ = size;
  return true;
}

bool Layer::SetColor(const escher::vec4& color) {
  color_ = color;
  return true;
}

bool Layer::Detach() {
  if (layer_stack_) {
    // Can't set layer-stack after detaching, because we might be destroyed (if
    // our ref-count hits zero).
    auto layer_stack = layer_stack_;
    layer_stack_ = nullptr;
    layer_stack->RemoveLayer(this);
  }
  return true;
}

void Layer::CollectScenes(std::set<Scene*>* scenes_out) {
  if (renderer_ && renderer_->camera() && renderer_->camera()->scene()) {
    scenes_out->insert(renderer_->camera()->scene().get());
  }
}

bool Layer::IsDrawable() const {
  if (size_ == escher::vec2(0, 0)) {
    return false;
  }

  // TODO(MZ-249): Layers can also have a material or image pipe.
  return renderer_ && renderer_->camera() && renderer_->camera()->scene();
}

}  // namespace scene_manager
