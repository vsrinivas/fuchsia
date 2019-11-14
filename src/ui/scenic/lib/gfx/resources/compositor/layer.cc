// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"

#include <optional>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/util/type_utils.h"
#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"
#include "src/ui/scenic/lib/gfx/resources/camera.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/resources/renderers/renderer.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"

namespace scenic_impl {
namespace gfx {

namespace {

std::optional<ViewHit> CreateViewHit(const NodeHit& hit, const escher::mat4& layer_transform) {
  FXL_DCHECK(hit.node);
  ViewPtr view = hit.node->FindOwningView();

  if (!view) {
    return std::nullopt;
  }

  FXL_DCHECK(view->GetViewNode());

  return ViewHit{
      .view = view,
      .transform = glm::inverse(view->GetViewNode()->GetGlobalTransform()) * layer_transform,
      .distance = hit.distance,
  };
}

}  // namespace

const ResourceTypeInfo Layer::kTypeInfo = {ResourceType::kLayer, "Layer"};

Layer::Layer(Session* session, SessionId session_id, ResourceId id)
    : Resource(session, session_id, id, Layer::kTypeInfo), translation_(0) {}

Layer::~Layer() = default;

bool Layer::SetRenderer(RendererPtr renderer) {
  // TODO(SCN-249): if layer content is already specified as an image, clear it
  // before setting the renderer.  Or call it an error, and require the client
  // to explicitly clear it first.
  renderer_ = std::move(renderer);
  return true;
}

bool Layer::SetSize(const escher::vec2& size, ErrorReporter* reporter) {
  if (size.x <= 0 || size.y <= 0) {
    if (size != escher::vec2(0, 0)) {
      reporter->ERROR() << "scenic::gfx::Layer::SetSize(): size must be positive";
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

bool Layer::Detach(ErrorReporter* reporter) {
  if (layer_stack_) {
    layer_stack_->RemoveLayer(this);
    FXL_DCHECK(!layer_stack_);  // removed by RemoveLayer().
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

  // TODO(SCN-249): Layers can also have a material or image pipe.
  return renderer_ && renderer_->camera() && renderer_->camera()->scene();
}

void Layer::HitTest(const escher::ray4& ray, HitAccumulator<ViewHit>* hit_accumulator) const {
  if (width() == 0.f || height() == 0.f) {
    return;
  }

  const Camera* const camera = renderer_->camera();

  // Normalize the origin of the ray with respect to the width and height of the
  // layer before passing it to the camera.
  const escher::mat4 layer_normalization =
      glm::scale(glm::vec3(1.f / width(), 1.f / height(), 1.f));

  const auto local_ray = layer_normalization * ray;

  // Transform the normalized ray by the camera's transformation.
  const auto [camera_ray, camera_transform] = camera->ProjectRay(local_ray, GetViewingVolume());

  // CreateViewHit needs this to compose the transform from layer space to view space.
  const escher::mat4 layer_transform = camera_transform * layer_normalization;

  MappingAccumulator<NodeHit, ViewHit> transforming_accumulator(
      hit_accumulator,
      [&layer_transform](const NodeHit& hit) { return CreateViewHit(hit, layer_transform); });

  gfx::HitTest(camera->scene().get(), camera_ray, &transforming_accumulator);
}

escher::ViewingVolume Layer::GetViewingVolume() const {
  // TODO(SCN-1276): Don't hardcode Z bounds in multiple locations.
  constexpr float kTop = -1000;
  constexpr float kBottom = 0;
  return escher::ViewingVolume(size_.x, size_.y, kTop, kBottom);
}

}  // namespace gfx
}  // namespace scenic_impl
