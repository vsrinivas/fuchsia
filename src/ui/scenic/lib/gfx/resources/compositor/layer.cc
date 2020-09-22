// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"

#include <lib/syslog/cpp/macros.h>

#include <optional>

#include "src/ui/lib/escher/util/type_utils.h"
#include "src/ui/scenic/lib/gfx/resources/camera.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/resources/renderers/renderer.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Layer::kTypeInfo = {ResourceType::kLayer, "Layer"};

Layer::Layer(Session* session, SessionId session_id, ResourceId id)
    : Resource(session, session_id, id, Layer::kTypeInfo), translation_(0) {}

Layer::~Layer() = default;

bool Layer::SetRenderer(RendererPtr renderer) {
  // TODO(fxbug.dev/23495): if layer content is already specified as an image, clear it
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
    FX_DCHECK(!layer_stack_);  // removed by RemoveLayer().
  }
  return true;
}

fxl::WeakPtr<Scene> Layer::scene() {
  if (!renderer_ || !renderer_->camera())
    return {};

  return renderer_->camera()->scene()->GetWeakPtr();
}

void Layer::CollectScenes(std::set<Scene*>* scenes_out) {
  fxl::WeakPtr scene = this->scene();
  if (scene) {
    scenes_out->insert(scene.get());
  }
}

bool Layer::IsDrawable() const {
  if (size_ == escher::vec2(0, 0)) {
    return false;
  }

  // TODO(fxbug.dev/23495): Layers can also have a material or image pipe.
  return renderer_ && renderer_->camera() && renderer_->camera()->scene();
}

escher::ViewingVolume Layer::GetViewingVolume() const {
  // TODO(fxbug.dev/24474): Don't hardcode Z bounds in multiple locations.
  constexpr float kTop = -1000;
  constexpr float kBottom = 0;
  return escher::ViewingVolume(size_.x, size_.y, kTop, kBottom);
}

std::optional<escher::mat4> Layer::GetWorldFromScreenTransform() const {
  if (!renderer_ || !renderer_->camera()) {
    return std::nullopt;
  }

  // Transform from pixel space [0, width] x [0, height] to Vulkan normalized device coordinates [0,
  // 1] x [0, 1].
  const escher::mat4 pixel_transform = glm::scale(glm::vec3(1.f / width(), 1.f / height(), 1.f));

  // Transform from Vulkan normalized device coordinates [0, 1] to the projection space of the
  // camera [-1, 1].
  const auto device_transform =
      glm::translate(glm::vec3(-1.f, -1.f, 0.f)) * glm::scale(glm::vec3(2.f, 2.f, 1.f));

  // Transform from projection space to world space (layer space).
  const auto vp_transform = renderer_->camera()->GetViewProjectionMatrix(GetViewingVolume());
  const auto camera_transform = glm::inverse(vp_transform);

  // Return transform from pixel space to world space.
  return camera_transform * device_transform * pixel_transform;
}

}  // namespace gfx
}  // namespace scenic_impl
