// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/renderer/renderer.h"

#include "apps/mozart/src/scene_manager/frame_scheduler.h"
#include "apps/mozart/src/scene_manager/resources/camera.h"
#include "apps/mozart/src/scene_manager/resources/import.h"
#include "apps/mozart/src/scene_manager/resources/material.h"
#include "apps/mozart/src/scene_manager/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene_manager/resources/nodes/node.h"
#include "apps/mozart/src/scene_manager/resources/nodes/scene.h"
#include "apps/mozart/src/scene_manager/resources/nodes/shape_node.h"
#include "apps/mozart/src/scene_manager/resources/nodes/traversal.h"
#include "apps/mozart/src/scene_manager/resources/shapes/circle_shape.h"
#include "apps/mozart/src/scene_manager/resources/shapes/shape.h"

namespace mozart {
namespace scene {

const ResourceTypeInfo Renderer::kTypeInfo = {ResourceType::kRenderer,
                                              "Renderer"};

Renderer::Renderer(Session* session,
                   ResourceId id,
                   FrameScheduler* frame_scheduler)
    : Resource(session, id, Renderer::kTypeInfo),
      frame_scheduler_(frame_scheduler) {
  FTL_DCHECK(frame_scheduler);
  escher::MaterialPtr default_material_ =
      ftl::MakeRefCounted<escher::Material>();
  default_material_->set_color(escher::vec3(0.f, 0.f, 0.f));
}

Renderer::~Renderer() {
  if (camera_) {
    frame_scheduler_->RemoveRenderer(this);
  }
}

std::vector<escher::Object> Renderer::CreateDisplayList(
    const ScenePtr& scene,
    escher::vec2 screen_dimensions) {
  // Construct a display list from the tree.
  Visitor v(default_material_);
  scene->Accept(&v);
  return v.TakeDisplayList();
}

void Renderer::SetCamera(CameraPtr camera) {
  if (!camera_ && !camera) {
    // Still no camera.
    return;
  } else if (camera_ && camera) {
    // Switch camera to new one.  No need to notify FrameScheduler.
    camera_ = std::move(camera);
  } else if (camera) {
    // Camera became non-null.  Register with FrameScheduler.
    camera_ = std::move(camera);
    frame_scheduler_->AddRenderer(this);
  } else {
    camera_ = nullptr;
    frame_scheduler_->RemoveRenderer(this);
  }
}

Renderer::Visitor::Visitor(const escher::MaterialPtr& default_material)
    : default_material_(default_material) {}

std::vector<escher::Object> Renderer::Visitor::TakeDisplayList() {
  return std::move(display_list_);
}

void Renderer::Visitor::Visit(GpuMemory* r) {
  FTL_CHECK(false);
}

void Renderer::Visitor::Visit(HostMemory* r) {
  FTL_CHECK(false);
}

void Renderer::Visitor::Visit(Image* r) {
  FTL_CHECK(false);
}

void Renderer::Visitor::Visit(ImagePipe* r) {
  FTL_CHECK(false);
}

void Renderer::Visitor::Visit(EntityNode* r) {
  VisitNode(r);
}

void Renderer::Visitor::VisitNode(Node* r) {
  // If not clipping, recursively visit all descendants in the normal fashion.
  if (!r->clip_to_self()) {
    ForEachDirectDescendantFrontToBack(
        *r, [this](Node* node) { node->Accept(this); });
    return;
  }

  // We might need to apply a clip.
  // Gather the escher::Objects corresponding to the children and imports.
  Renderer::Visitor clippee_visitor(default_material_);
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
  Renderer::Visitor clipper_visitor(kNoMaterial);
  ForEachPartFrontToBack(*r, [&clipper_visitor](Node* node) {
    if (node->IsKindOf<ShapeNode>()) {
      node->Accept(&clipper_visitor);
    } else {
      // TODO(MZ-167): accept non-ShapeNode parts.  This might already work
      // (i.e. it might be as simple as saying
      // "part->Accept(&part_visitor)"), but this hasn't been tested.
      FTL_LOG(WARNING) << "Renderer::Visitor::VisitNode(): Clipping only "
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

void Renderer::Visitor::Visit(Scene* r) {
  VisitNode(r);
}

void Renderer::Visitor::Visit(ShapeNode* r) {
  auto& shape = r->shape();
  auto& material = r->material();
  if (material) {
    material->Accept(this);
  }
  if (shape) {
    display_list_.push_back(shape->GenerateRenderObject(
        r->GetGlobalTransform(),
        material ? material->escher_material() : default_material_));
  }
  // We don't need to call |VisitNode| because shape nodes don't have
  // children or parts.
}

void Renderer::Visitor::Visit(CircleShape* r) {
  FTL_CHECK(false);
}

void Renderer::Visitor::Visit(RectangleShape* r) {
  FTL_CHECK(false);
}

void Renderer::Visitor::Visit(RoundedRectangleShape* r) {
  FTL_CHECK(false);
}

void Renderer::Visitor::Visit(Material* r) {
  r->UpdateEscherMaterial();
}

void Renderer::Visitor::Visit(Import* r) {
  FTL_CHECK(false);
}

void Renderer::Visitor::Visit(Camera* r) {
  // TODO: use camera's projection matrix.
  Visit(r->scene().get());
}

void Renderer::Visitor::Visit(Renderer* r) {
  FTL_CHECK(false);
}

void Renderer::Visitor::Visit(DirectionalLight* r) {
  FTL_CHECK(false);
}

}  // namespace scene
}  // namespace mozart
