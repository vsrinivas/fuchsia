// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/renderers/renderer.h"

#include <trace/event.h>

#include "src/ui/lib/escher/scene/model.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/camera.h"
#include "src/ui/scenic/lib/gfx/resources/dump_visitor.h"
#include "src/ui/scenic/lib/gfx/resources/material.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/entity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/opacity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/traversal.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/circle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/shape.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Renderer::kTypeInfo = {ResourceType::kRenderer, "Renderer"};

Renderer::Renderer(Session* session, SessionId session_id, ResourceId id)
    : Resource(session, session_id, id, Renderer::kTypeInfo) {
  escher::MaterialPtr default_material_ = fxl::MakeRefCounted<escher::Material>();
  default_material_->set_color(escher::vec3(0.f, 0.f, 0.f));
}

Renderer::~Renderer() = default;

void Renderer::SetCamera(CameraPtr camera) { camera_ = std::move(camera); }

bool Renderer::SetShadowTechnique(::fuchsia::ui::gfx::ShadowTechnique technique) {
  shadow_technique_ = technique;
  return true;
}

void Renderer::DisableClipping(bool disable_clipping) { disable_clipping_ = disable_clipping; }

}  // namespace gfx
}  // namespace scenic_impl
