// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/renderer/renderer.h"

#include "apps/mozart/src/scene/frame_scheduler.h"
#include "apps/mozart/src/scene/resources/material.h"
#include "apps/mozart/src/scene/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene/resources/nodes/node.h"
#include "apps/mozart/src/scene/resources/nodes/scene.h"
#include "apps/mozart/src/scene/resources/nodes/shape_node.h"
#include "apps/mozart/src/scene/resources/nodes/tag_node.h"
#include "apps/mozart/src/scene/resources/import.h"
#include "apps/mozart/src/scene/resources/shapes/circle_shape.h"
#include "apps/mozart/src/scene/resources/shapes/shape.h"

namespace mozart {
namespace scene {

Renderer::Renderer(FrameScheduler* frame_scheduler)
    : frame_scheduler_(frame_scheduler) {
  FTL_DCHECK(frame_scheduler);
  frame_scheduler_->AddRenderer(this);
}

Renderer::~Renderer() {
  frame_scheduler_->RemoveRenderer(this);
}

std::vector<escher::Object> Renderer::CreateDisplayList(
    Node* root_node,
    escher::vec2 screen_dimensions) {
  // Construct a display list from the tree.
  Visitor v;
  root_node->Accept(&v);
  std::vector<escher::Object> objects = v.TakeDisplayList();

  // Add a background.
  auto background_material = ftl::MakeRefCounted<escher::Material>();
  background_material->set_color(escher::vec3(0.8f, 0.8f, 0.8f));
  objects.push_back(escher::Object::NewRect(
      escher::vec2(0.f, 0.f), screen_dimensions, 0.f, background_material));

  return objects;
}

void Renderer::set_scene(Scene* scene) {
  FTL_DCHECK(!scene_ && scene || scene_ && !scene);
  scene_ = scene;
}

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

void Renderer::Visitor::Visit(EntityNode* r) {
  VisitNode(r);
}

void Renderer::Visitor::Visit(TagNode* r) {
  VisitNode(r);
}

void Renderer::Visitor::VisitNode(Node* r) {
  for (auto& child : r->children()) {
    child->Accept(this);
  }
  for (auto& part : r->parts()) {
    part->Accept(this);
  }
  for (auto& import : r->imports()) {
    import->delegate()->Accept(this);
  }
}

void Renderer::Visitor::Visit(Scene* r) {
  VisitNode(r);
}

void Renderer::Visitor::Visit(ShapeNode* r) {
  auto& shape = r->shape();
  auto& material = r->material();
  if (shape && material) {
    display_list_.push_back(shape->GenerateRenderObject(
        r->GetGlobalTransform(), material->escher_material()));
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
  FTL_CHECK(false);
}

void Renderer::Visitor::Visit(Import* r) {
  FTL_CHECK(false);
}

}  // namespace scene
}  // namespace mozart
