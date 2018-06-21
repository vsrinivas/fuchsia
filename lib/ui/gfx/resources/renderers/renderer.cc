// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"

#include <trace/event.h>

#include "lib/escher/impl/ssdo_sampler.h"
#include "lib/escher/renderer/renderer.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/scene/stage.h"

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

namespace scenic {
namespace gfx {

const ResourceTypeInfo Renderer::kTypeInfo = {ResourceType::kRenderer,
                                              "Renderer"};

const uint32_t Renderer::kRequiredSwapchainPixelMultiple =
    escher::impl::SsdoSampler::kSsdoAccelDownsampleFactor;

Renderer::Renderer(Session* session, scenic::ResourceId id)
    : Resource(session, id, Renderer::kTypeInfo) {
  escher::MaterialPtr default_material_ =
      fxl::MakeRefCounted<escher::Material>();
  default_material_->set_color(escher::vec3(0.f, 0.f, 0.f));
}

Renderer::~Renderer() = default;

std::vector<escher::Object> Renderer::CreateDisplayList(
    const ScenePtr& scene, escher::vec2 screen_dimensions) {
  TRACE_DURATION("gfx", "Renderer::CreateDisplayList");

  // Construct a display list from the tree.
  Visitor v(default_material_, 1, disable_clipping_);
  scene->Accept(&v);
  return v.TakeDisplayList();
}

void Renderer::SetCamera(CameraPtr camera) { camera_ = std::move(camera); }

bool Renderer::SetShadowTechnique(
    ::fuchsia::ui::gfx::ShadowTechnique technique) {
  shadow_technique_ = technique;
  return true;
}

void Renderer::SetRenderContinuously(bool render_continuously) {
  session()->engine()->frame_scheduler()->SetRenderContinuously(
      render_continuously);
}

void Renderer::DisableClipping(bool disable_clipping) {
  disable_clipping_ = disable_clipping;
}

Renderer::Visitor::Visitor(const escher::MaterialPtr& default_material,
                           float opacity, bool disable_clipping)
    : default_material_(default_material),
      opacity_(opacity),
      disable_clipping_(disable_clipping) {}

std::vector<escher::Object> Renderer::Visitor::TakeDisplayList() {
  return std::move(display_list_);
}

void Renderer::Visitor::Visit(GpuMemory* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(HostMemory* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(Image* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(ImagePipe* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(Buffer* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(View* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(ViewHolder* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(EntityNode* r) { VisitNode(r); }

void Renderer::Visitor::Visit(OpacityNode* r) {
  if (r->opacity() == 0) {
    return;
  }

  float old_opacity = opacity_;
  opacity_ *= r->opacity();

  VisitNode(r);

  opacity_ = old_opacity;
}

void Renderer::Visitor::VisitNode(Node* r) {
  // If not clipping, recursively visit all descendants in the normal fashion.
  if (!r->clip_to_self() || disable_clipping_) {
    ForEachDirectDescendantFrontToBack(
        *r, [this](Node* node) { node->Accept(this); });
    return;
  }

  // We might need to apply a clip.
  // Gather the escher::Objects corresponding to the children and imports.
  Renderer::Visitor clippee_visitor(default_material_, opacity_,
                                    disable_clipping_);
  ForEachChildAndImportFrontToBack(
      *r, [&clippee_visitor](Node* node) { node->Accept(&clippee_visitor); });

  // Check whether there's anything to clip.
  auto clippees = clippee_visitor.TakeDisplayList();
  if (clippees.empty()) {
    // Nothing to clip!  Just draw the parts as usual.
    ForEachPartFrontToBack(*r, [this](Node* node) { node->Accept(this); });
    return;
  }

  // The node's children and imports must be clipped by the
  // Shapes/ShapeNodes amongst the node's parts.  First gather the
  // escher::Objects corresponding to these ShapeNodes.
  const escher::MaterialPtr kNoMaterial;
  Renderer::Visitor clipper_visitor(kNoMaterial, opacity_, disable_clipping_);
  ForEachPartFrontToBack(*r, [&clipper_visitor](Node* node) {
    if (node->IsKindOf<ShapeNode>()) {
      node->Accept(&clipper_visitor);
    } else {
      // TODO(MZ-167): accept non-ShapeNode parts.  This might already work
      // (i.e. it might be as simple as saying
      // "part->Accept(&part_visitor)"), but this hasn't been tested.
      FXL_LOG(WARNING) << "Renderer::Visitor::VisitNode(): Clipping only "
                          "supports ShapeNode parts.";
    }
  });

  // Check whether there are any clippers.
  auto clippers = clipper_visitor.TakeDisplayList();
  if (clippers.empty()) {
    // The clip is empty so there's nothing to draw.
    return;
  }

  // Some chicanery is required to draw in the order specified by
  // ForEachDirectDescendantFrontToBack().  Namely, all clippers that are
  // also visible (i.e. have a non-null material) need to be drawn twice:
  // once as a clipper (with the material removed), and later as a clippee
  // (with the material intact).
  // TODO(MZ-176): are there some constraints that we can put on allowable
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
        material ? material->escher_material() : default_material_;
    if (escher_material && opacity_ < 1) {
      // When we want to support other material types (e.g. metallic shaders),
      // we'll need to change this. If we want to support semitransparent
      // textures and materials, we'll need more pervasive changes.
      glm::vec4 color = escher_material->color();
      color.a *= opacity_;
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

void Renderer::Visitor::Visit(Material* r) { r->UpdateEscherMaterial(); }

void Renderer::Visitor::Visit(Import* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(Camera* r) {
  // TODO: use camera's projection matrix.
  Visit(r->scene().get());
}

void Renderer::Visitor::Visit(Renderer* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(Light* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(AmbientLight* r) { FXL_CHECK(false); }

void Renderer::Visitor::Visit(DirectionalLight* r) { FXL_CHECK(false); }

}  // namespace gfx
}  // namespace scenic
