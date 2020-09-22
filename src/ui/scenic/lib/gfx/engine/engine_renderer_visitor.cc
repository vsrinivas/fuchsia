// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/engine_renderer_visitor.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/lib/escher/paper/paper_renderer.h"
#include "src/ui/scenic/lib/gfx/resources/camera.h"
#include "src/ui/scenic/lib/gfx/resources/image_base.h"
#include "src/ui/scenic/lib/gfx/resources/material.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/entity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/opacity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/traversal.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/view_node.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/circle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/mesh_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/rectangle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/shape.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"

namespace scenic_impl {
namespace gfx {

EngineRendererVisitor::EngineRendererVisitor(escher::PaperRenderer* renderer,
                                             escher::BatchGpuUploader* gpu_uploader,
                                             escher::ImageLayoutUpdater* layout_updater,
                                             bool hide_protected_memory,
                                             escher::MaterialPtr replacement_material)
    : renderer_(renderer),
      gpu_uploader_(gpu_uploader),
      layout_updater_(layout_updater),
      hide_protected_memory_(hide_protected_memory),
      replacement_material_(replacement_material) {}

void EngineRendererVisitor::Visit(Memory* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(Image* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(ImagePipeBase* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(Buffer* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(View* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(ViewNode* r) {
  const size_t previous_count = draw_call_count_;
  const bool previous_should_render_debug_bounds = should_render_debug_bounds_;
  if (auto view = r->GetView()) {
    should_render_debug_bounds_ = view->should_render_bounding_box();
  }
  VisitNode(r);

  bool view_is_rendering_element = draw_call_count_ > previous_count;
  if (r->GetView() && view_is_rendering_element) {
    // TODO(fxbug.dev/24307) Add a test to ensure this signal isn't triggered when this
    // view is not rendering.
    r->GetView()->SignalRender();
  }

  // Render all the annotation ViewHolders.
  if (r->GetView()) {
    for (const auto annotation_view_holder : r->GetView()->annotation_view_holders()) {
      Visit(annotation_view_holder.get());
    }
  }

  should_render_debug_bounds_ = previous_should_render_debug_bounds;
}

void EngineRendererVisitor::Visit(ViewHolder* r) {
  escher::PaperTransformStack* transform_stack = renderer_->transform_stack();
  transform_stack->PushTransform(static_cast<escher::mat4>(r->transform()));
  transform_stack->AddClipPlanes(r->clip_planes());

  // A view holder should render its bounds if either its embedding view has
  // debug rendering turned on (which will mean should_render_debug_bounds_=true)
  // or if its own view specifies that debug bounds should be rendered.
  if (should_render_debug_bounds_ || (r->view() && r->view()->should_render_bounding_box())) {
    auto bbox = r->GetLocalBoundingBox();
    // Create material and submit draw call.
    escher::PaperMaterialPtr escher_material = escher::Material::New(r->bounds_color());
    escher_material->set_type(escher::Material::Type::kWireframe);
    renderer_->DrawBoundingBox(bbox, escher_material, escher::PaperDrawableFlags());
    ++draw_call_count_;
  }

  ForEachChildFrontToBack(*r, [this](Node* node) { node->Accept(this); });
  transform_stack->Pop();
}

void EngineRendererVisitor::Visit(EntityNode* r) { VisitNode(r); }

void EngineRendererVisitor::Visit(OpacityNode* r) {
  if (r->opacity() == 0) {
    return;
  }

  float old_opacity = opacity_;
  opacity_ *= r->opacity();

  VisitNode(r);

  opacity_ = old_opacity;
}

void EngineRendererVisitor::VisitNode(Node* r) {
  escher::PaperTransformStack* transform_stack = renderer_->transform_stack();
  transform_stack->PushTransform(static_cast<escher::mat4>(r->transform()));
  transform_stack->AddClipPlanes(r->clip_planes());

  ForEachChildFrontToBack(*r, [this](Node* node) { node->Accept(this); });

  transform_stack->Pop();
}

void EngineRendererVisitor::Visit(Scene* r) { VisitNode(r); }

void EngineRendererVisitor::Visit(Compositor* r) { FX_DCHECK(false); }

void EngineRendererVisitor::Visit(DisplayCompositor* r) { FX_DCHECK(false); }

void EngineRendererVisitor::Visit(LayerStack* r) { FX_DCHECK(false); }

void EngineRendererVisitor::Visit(Layer* r) { FX_DCHECK(false); }

void EngineRendererVisitor::Visit(ShapeNode* r) {
  // We don't need to call |VisitNode| because shape nodes don't have children.
  FX_DCHECK(r->children().empty());

  auto& shape = r->shape();
  auto& material = r->material();
  if (!material || !shape)
    return;

  material->Accept(this);

  escher::MaterialPtr escher_material = material->escher_material();
  FX_DCHECK(escher_material);

  if (hide_protected_memory_ && material->texture_image() &&
      material->texture_image()->use_protected_memory()) {
    FX_DCHECK(replacement_material_);
    escher_material = replacement_material_;
  }

  if (opacity_ < 1.f) {
    // When we want to support other material types (e.g. metallic shaders),
    // we'll need to change this. If we want to support semitransparent
    // textures and materials, we'll need more pervasive changes.
    glm::vec4 color = escher_material->color();
    color.a *= opacity_;
    escher_material = escher::Material::New(color, escher_material->texture());
    escher_material->set_type(escher::Material::Type::kTranslucent);
  }

  escher::PaperTransformStack* transform_stack = renderer_->transform_stack();
  transform_stack->PushTransform(static_cast<escher::mat4>(r->transform()));

  escher::PaperDrawableFlags flags{};
  if (shape->IsKindOf<RoundedRectangleShape>()) {
    auto rect = static_cast<RoundedRectangleShape*>(shape.get());
    renderer_->DrawRoundedRect(rect->spec(), escher_material, flags);
  } else if (shape->IsKindOf<RectangleShape>()) {
    auto rect = static_cast<RectangleShape*>(shape.get());
    renderer_->DrawRect(rect->width(), rect->height(), escher_material, flags);
  } else if (shape->IsKindOf<CircleShape>()) {
    auto circle = static_cast<CircleShape*>(shape.get());

    // Only draw the circle if its radius is greater than epsilon.
    if (circle->radius() > escher::kEpsilon) {
      renderer_->DrawCircle(circle->radius(), escher_material, flags);
    }
  } else if (shape->IsKindOf<MeshShape>()) {
    auto mesh_shape = static_cast<MeshShape*>(shape.get());
    renderer_->DrawMesh(mesh_shape->escher_mesh(), escher_material, flags);
  } else {
    FX_LOGS(ERROR) << "Unsupported shape type encountered.";
  }
  transform_stack->Pop();

  ++draw_call_count_;
}

void EngineRendererVisitor::Visit(CircleShape* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(RectangleShape* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(RoundedRectangleShape* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(MeshShape* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(Material* r) {
  r->UpdateEscherMaterial(gpu_uploader_, layout_updater_);
}

void EngineRendererVisitor::Visit(Camera* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(Renderer* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(Light* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(AmbientLight* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(DirectionalLight* r) { FX_CHECK(false); }

void EngineRendererVisitor::Visit(PointLight* r) { FX_CHECK(false); }

}  // namespace gfx
}  // namespace scenic_impl
