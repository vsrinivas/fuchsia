// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/resources/compositor/layer.h"

#include "garnet/lib/ui/scenic/engine/hit_tester.h"
#include "garnet/lib/ui/scenic/resources/camera.h"
#include "garnet/lib/ui/scenic/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/scenic/resources/renderers/renderer.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "garnet/public/lib/escher/util/type_utils.h"

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

std::vector<Hit> Layer::HitTest(const escher::ray4& ray,
                                Session* session) const {
  Camera* camera = renderer()->camera();

  if (width() == 0.f || height() == 0.f) {
    return std::vector<Hit>();
  }

  // Normalize the origin of the ray with respect to the width and height of the
  // layer before passing it to the camera.
  escher::mat4 layer_normalization =
      glm::scale(glm::vec3(1.f / width(), 1.f / height(), 1.f));

  auto local_ray = layer_normalization * ray;

  // Transform the normalized ray by the camera's transformation.
  std::pair<escher::ray4, escher::mat4> camera_projection_pair =
      camera->ProjectRayIntoScene(local_ray, GetViewingVolume());

  HitTester hit_tester;
  std::vector<Hit> hits = hit_tester.HitTest(
      camera->scene().get(), camera_projection_pair.first, session);

  escher::mat4 inverse_layer_transform =
      glm::inverse(camera_projection_pair.second * layer_normalization);

  // Take the camera's transformation into account; after this the hit's
  // inverse_transform goes from the passed in ray's coordinate system to the
  // hit nodes' coordinate system.
  for (auto& hit : hits) {
    hit.inverse_transform =
        inverse_layer_transform * glm::inverse(hit.inverse_transform);
  }

  return hits;
}

escher::ViewingVolume Layer::GetViewingVolume() const {
  // TODO(MZ-194): Define these properties somewhere better (perhaps Scene?)
  // instead of hardcoding them here.
  constexpr float kTop = 1000;
  constexpr float kBottom = 0;
  return escher::ViewingVolume(size_.x, size_.y, kTop, kBottom);
}

}  // namespace scene_manager
