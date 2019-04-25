// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"

#include <lib/escher/renderer/renderer.h>
#include <lib/escher/scene/model.h>
#include <lib/escher/scene/stage.h>
#include <trace/event.h>

#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/camera.h"
#include "garnet/lib/ui/gfx/resources/dump_visitor.h"
#include "garnet/lib/ui/gfx/resources/import.h"
#include "garnet/lib/ui/gfx/resources/material.h"
#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "garnet/lib/ui/gfx/resources/nodes/opacity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/scene.h"
#include "garnet/lib/ui/gfx/resources/nodes/shape_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/traversal.h"
#include "garnet/lib/ui/gfx/resources/shapes/circle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/shape.h"
#include "garnet/lib/ui/gfx/resources/view.h"
#include "garnet/lib/ui/gfx/resources/view_holder.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Renderer::kTypeInfo = {ResourceType::kRenderer,
                                              "Renderer"};

Renderer::Renderer(Session* session, ResourceId id)
    : Resource(session, id, Renderer::kTypeInfo) {
  escher::MaterialPtr default_material_ =
      fxl::MakeRefCounted<escher::Material>();
  default_material_->set_color(escher::vec3(0.f, 0.f, 0.f));
}

Renderer::~Renderer() = default;

std::vector<escher::Object> Renderer::CreateDisplayList(
    const ScenePtr& scene, escher::vec2 screen_dimensions,
    escher::BatchGpuUploader* uploader) {
  TRACE_DURATION("gfx", "Renderer::CreateDisplayList");

  VisitorContext visitor_context(default_material_, /* opacity= */ 1.0f,
                                 disable_clipping_, uploader);

  // Construct a display list from the tree.
  Visitor v(std::move(visitor_context));
  scene->Accept(&v);

  return v.TakeDisplayList();
}

void Renderer::SetCamera(CameraPtr camera) { camera_ = std::move(camera); }

bool Renderer::SetShadowTechnique(
    ::fuchsia::ui::gfx::ShadowTechnique technique) {
  shadow_technique_ = technique;
  return true;
}

void Renderer::DisableClipping(bool disable_clipping) {
  disable_clipping_ = disable_clipping;
}

Renderer::Visitor::Visitor(Renderer::VisitorContext context)
    : context_(context) {}

std::vector<escher::Object> Renderer::Visitor::TakeDisplayList() {
  return std::move(display_list_);
}

void Renderer::Visitor::Visit(Memory* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(Image* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(ImagePipe* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(Buffer* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(View* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(ViewNode* r) {
  size_t previous_display_size = display_list_.size();

  VisitNode(r);

  bool view_is_rendering_element = display_list_.size() > previous_display_size;
  if (r->GetView() && view_is_rendering_element) {
    // TODO(SCN-1099) Add a test to ensure this signal isn't triggered when this
    // view is not rendering.
    r->GetView()->SignalRender();
  }
}

void Renderer::Visitor::Visit(ViewHolder* r) { VisitNode(r); }

void Renderer::Visitor::Visit(EntityNode* r) { VisitNode(r); }

void Renderer::Visitor::Visit(OpacityNode* r) {
  if (r->opacity() == 0) {
    return;
  }

  float old_opacity = context_.opacity;
  context_.opacity *= r->opacity();

  VisitNode(r);

  context_.opacity = old_opacity;
}

void Renderer::Visitor::VisitNode(Node* r) { VisitAndMaybeClipNode(r); }

std::vector<escher::Object> Renderer::Visitor::GenerateClippeeDisplayList(
    Node* r) {
  // Gather the escher::Objects corresponding to the children and imports.
  VisitorContext clippee_context(context_);
  Renderer::Visitor clippee_visitor(clippee_context);
  ForEachChildAndImportFrontToBack(
      *r, [&clippee_visitor](Node* node) { node->Accept(&clippee_visitor); });

  return clippee_visitor.TakeDisplayList();
}

std::vector<escher::Object> Renderer::Visitor::GenerateClipperDisplayList(
    Node* r) {
  // Create a VisitorContext with no material for the clippers.
  const escher::MaterialPtr kNoMaterial;
  VisitorContext clipper_context(kNoMaterial, context_.opacity,
                                 context_.disable_clipping,
                                 context_.batch_gpu_uploader);

  // The node's children and imports must be clipped by the
  // Shapes/ShapeNodes amongst the node's parts.  First gather the
  // escher::Objects corresponding to these ShapeNodes.
  Renderer::Visitor clipper_visitor(clipper_context);
  ForEachPartFrontToBack(*r, [&clipper_visitor](Node* node) {
    if (node->IsKindOf<ShapeNode>()) {
      node->Accept(&clipper_visitor);
    } else {
      // TODO(SCN-167): accept non-ShapeNode parts.  This might already work
      // (i.e. it might be as simple as saying
      // "part->Accept(&part_visitor)"), but this hasn't been tested.
      FXL_LOG(WARNING) << "Renderer::Visitor::VisitNode(): Clipping only "
                          "supports ShapeNode parts.";
    }
  });

  return clipper_visitor.TakeDisplayList();
}

void Renderer::Visitor::VisitAndMaybeClipNode(Node* r) {
  // If not clipping, recursively visit all descendants in the normal fashion.
  if (!r->clip_to_self() || context_.disable_clipping) {
    ForEachDirectDescendantFrontToBack(
        *r, [this](Node* node) { node->Accept(this); });
    return;
  }

  // Check whether there's anything to clip.
  auto clippees = GenerateClippeeDisplayList(r);
  if (clippees.empty()) {
    // Nothing to clip!  Just draw the parts as usual.
    ForEachPartFrontToBack(*r, [this](Node* node) { node->Accept(this); });
    return;
  }

  // Gather the objects used to form the clip regions.
  auto clippers = GenerateClipperDisplayList(r);
  if (clippers.empty()) {
    // The clip is empty so there's nothing to draw.
    return;
  }

  // Some chicanery is required to draw in the order specified by
  // ForEachDirectDescendantFrontToBack().  Namely, all clippers that are
  // also visible (i.e. have a non-null material) need to be drawn twice:
  // once as a clipper (with the material removed), and later as a clippee
  // (with the material intact).
  // TODO(SCN-176): are there some constraints that we can put on allowable
  // elevations that would allow us to relax the draw-order constraint,
  // and thereby not render the objects twice?
  for (auto& obj : clippers) {
    if (obj.material()) {
      clippees.push_back(obj);
      obj.set_material(escher::MaterialPtr());
    }
  }

  // Create a new "clip object" from the display-lists generated by the
  // two visitors above.
  display_list_.push_back(
      escher::Object(std::move(clippers), std::move(clippees)));
}

void Renderer::Visitor::Visit(Scene* r) { VisitNode(r); }

void Renderer::Visitor::Visit(Compositor* r) { FXL_DCHECK(false); }

void Renderer::Visitor::Visit(DisplayCompositor* r) { FXL_DCHECK(false); }

void Renderer::Visitor::Visit(LayerStack* r) { FXL_DCHECK(false); }

void Renderer::Visitor::Visit(Layer* r) { FXL_DCHECK(false); }

void Renderer::Visitor::Visit(ShapeNode* r) {
  auto& shape = r->shape();
  auto& material = r->material();
  if (material) {
    material->Accept(this);
  }
  if (shape) {
    escher::MaterialPtr escher_material =
        material ? material->escher_material() : context_.default_material;
    if (escher_material && context_.opacity < 1) {
      // When we want to support other material types (e.g. metallic shaders),
      // we'll need to change this. If we want to support semitransparent
      // textures and materials, we'll need more pervasive changes.
      glm::vec4 color = escher_material->color();
      color.a *= context_.opacity;
      escher_material =
          escher::Material::New(color, escher_material->texture());
      escher_material->set_opaque(false);
    }

    display_list_.push_back(
        shape->GenerateRenderObject(r->GetGlobalTransform(), escher_material));
  }
  // We don't need to call |VisitNode| because shape nodes don't have
  // children or parts.
}

void Renderer::Visitor::Visit(CircleShape* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(RectangleShape* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(RoundedRectangleShape* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(MeshShape* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(Material* r) {
  r->UpdateEscherMaterial(context_.batch_gpu_uploader);
}

void Renderer::Visitor::Visit(Import* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(Camera* r) {
  // TODO: use camera's projection matrix.
  Visit(r->scene().get());
}

void Renderer::Visitor::Visit(Renderer* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(Light* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(AmbientLight* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(DirectionalLight* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(PointLight* r) { FXL_CHECK(false); }

}  // namespace gfx
}  // namespace scenic_impl
