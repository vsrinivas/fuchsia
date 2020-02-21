// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/has_renderable_content_visitor.h"

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/gfx/resources/camera.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/image.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe_base.h"
#include "src/ui/scenic/lib/gfx/resources/material.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/entity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/opacity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/renderers/renderer.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/circle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/mesh_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/rectangle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"

namespace scenic_impl {
namespace gfx {

void HasRenderableContentVisitor::Visit(Memory* r) {}

void HasRenderableContentVisitor::Visit(Image* r) {}

void HasRenderableContentVisitor::Visit(ImagePipeBase* r) {}

void HasRenderableContentVisitor::Visit(Buffer* r) {}

void HasRenderableContentVisitor::Visit(View* r) {}

void HasRenderableContentVisitor::Visit(ViewNode* r) { VisitNode(r); }

void HasRenderableContentVisitor::Visit(ViewHolder* r) { VisitNode(r); }

void HasRenderableContentVisitor::Visit(EntityNode* r) { VisitNode(r); }

void HasRenderableContentVisitor::Visit(OpacityNode* r) { VisitNode(r); }

void HasRenderableContentVisitor::Visit(ShapeNode* r) {
  if (r->material()) {
    has_renderable_content_ = true;
  }
  VisitNode(r);
}

void HasRenderableContentVisitor::Visit(Scene* r) { VisitNode(r); }

void HasRenderableContentVisitor::Visit(CircleShape* r) { VisitResource(r); }

void HasRenderableContentVisitor::Visit(RectangleShape* r) { VisitResource(r); }

void HasRenderableContentVisitor::Visit(RoundedRectangleShape* r) { VisitResource(r); }

void HasRenderableContentVisitor::Visit(MeshShape* r) { VisitResource(r); }

void HasRenderableContentVisitor::Visit(Material* r) {}

void HasRenderableContentVisitor::Visit(Compositor* r) {}

void HasRenderableContentVisitor::Visit(DisplayCompositor* r) {}

void HasRenderableContentVisitor::Visit(LayerStack* r) {}

void HasRenderableContentVisitor::Visit(Layer* r) {
  if (has_renderable_content_) {
    return;
  }
  if (r->renderer()) {
    r->renderer()->Accept(this);
  }
}

void HasRenderableContentVisitor::Visit(Camera* r) { r->scene()->Accept(this); }

void HasRenderableContentVisitor::Visit(Renderer* r) {
  if (r->camera()) {
    r->camera()->Accept(this);
  }
}

void HasRenderableContentVisitor::Visit(Light* r) {}

void HasRenderableContentVisitor::Visit(AmbientLight* r) {}

void HasRenderableContentVisitor::Visit(DirectionalLight* r) {}

void HasRenderableContentVisitor::Visit(PointLight* r) {}

void HasRenderableContentVisitor::VisitNode(Node* r) {
  if (has_renderable_content_) {
    return;
  }
  for (auto& child : r->children()) {
    child->Accept(this);
  }
  VisitResource(r);
}

void HasRenderableContentVisitor::VisitResource(Resource* r) {}

}  // namespace gfx
}  // namespace scenic_impl
