// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/renderer/renderer.h"

#include "apps/mozart/src/scene/resources/link.h"
#include "apps/mozart/src/scene/resources/material.h"
#include "apps/mozart/src/scene/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene/resources/nodes/node.h"
#include "apps/mozart/src/scene/resources/nodes/shape_node.h"
#include "apps/mozart/src/scene/resources/shapes/circle_shape.h"
#include "apps/mozart/src/scene/resources/shapes/shape.h"

namespace mozart {
namespace composer {

Renderer::Renderer() {}

Renderer::~Renderer() {}

std::vector<escher::Object> Renderer::CreateDisplayList(
    Node* root_node,
    escher::vec2 screen_dimensions) {
  FTL_LOG(INFO) << "Renderer::DrawFrame";

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

std::vector<escher::Object> Renderer::Visitor::TakeDisplayList() {
  return std::move(display_list_);
};

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
  Visit(static_cast<Node*>(r));
}

void Renderer::Visitor::Visit(Node* r) {
  for (auto& child : r->children()) {
    child->Accept(this);
  }
  for (auto& part : r->parts()) {
    part->Accept(this);
  }
}

void Renderer::Visitor::Visit(ShapeNode* r) {
  auto& shape = r->shape();
  auto& material = r->material();
  if (!shape || !material)
    return;

  if (shape->IsKindOf<CircleShape>()) {
    auto circle = static_cast<CircleShape*>(shape.get());
    auto& transform = r->GetGlobalTransform();

    escher::vec4 origin = transform * escher::vec4(0, 0, 0, 1);
    auto object = escher::Object::NewCircle(escher::vec2(origin.x, origin.y),
                                            circle->radius(), origin.z,
                                            material->escher_material());
    display_list_.push_back(object);
  } else {
    // TODO: Render other shapes.
    FTL_CHECK(false);
  }
}

void Renderer::Visitor::Visit(CircleShape* r) {
  FTL_CHECK(false);
}

void Renderer::Visitor::Visit(Shape* r) {
  FTL_CHECK(false);
}

void Renderer::Visitor::Visit(Link* r) {
  Visit(static_cast<Node*>(r));
}

void Renderer::Visitor::Visit(Material* r) {
  FTL_CHECK(false);
}

}  // namespace composer
}  // namespace mozart
