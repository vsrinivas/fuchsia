// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/protected_memory_visitor.h"

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

void ProtectedMemoryVisitor::Visit(Memory* r) { VisitResource(r); }

void ProtectedMemoryVisitor::Visit(Image* r) {
  if (r->use_protected_memory()) {
    has_protected_memory_use_ = true;
  }
}

void ProtectedMemoryVisitor::Visit(ImagePipeBase* r) {
  if (r->use_protected_memory()) {
    has_protected_memory_use_ = true;
  }
}

void ProtectedMemoryVisitor::Visit(Buffer* r) {}

void ProtectedMemoryVisitor::Visit(View* r) {}

void ProtectedMemoryVisitor::Visit(ViewNode* r) { VisitNode(r); }

void ProtectedMemoryVisitor::Visit(ViewHolder* r) { VisitNode(r); }

void ProtectedMemoryVisitor::Visit(EntityNode* r) { VisitNode(r); }

void ProtectedMemoryVisitor::Visit(OpacityNode* r) { VisitNode(r); }

void ProtectedMemoryVisitor::Visit(ShapeNode* r) {
  if (r->material()) {
    r->material()->Accept(this);
  }
  VisitNode(r);
}

void ProtectedMemoryVisitor::Visit(Scene* r) { VisitNode(r); }

void ProtectedMemoryVisitor::Visit(CircleShape* r) { VisitResource(r); }

void ProtectedMemoryVisitor::Visit(RectangleShape* r) { VisitResource(r); }

void ProtectedMemoryVisitor::Visit(RoundedRectangleShape* r) { VisitResource(r); }

void ProtectedMemoryVisitor::Visit(MeshShape* r) { VisitResource(r); }

void ProtectedMemoryVisitor::Visit(Material* r) {
  if (auto backing_image = r->texture_image()) {
    if (backing_image->use_protected_memory()) {
      has_protected_memory_use_ = true;
    }
  }
  VisitResource(r);
}

void ProtectedMemoryVisitor::Visit(Compositor* r) {}

void ProtectedMemoryVisitor::Visit(DisplayCompositor* r) {}

void ProtectedMemoryVisitor::Visit(LayerStack* r) {}

void ProtectedMemoryVisitor::Visit(Layer* r) {
  if (r->renderer()) {
    r->renderer()->Accept(this);
  }
}

void ProtectedMemoryVisitor::Visit(Camera* r) { r->scene()->Accept(this); }

void ProtectedMemoryVisitor::Visit(Renderer* r) {
  if (r->camera()) {
    r->camera()->Accept(this);
  }
}

void ProtectedMemoryVisitor::Visit(Light* r) {}

void ProtectedMemoryVisitor::Visit(AmbientLight* r) {}

void ProtectedMemoryVisitor::Visit(DirectionalLight* r) {}

void ProtectedMemoryVisitor::Visit(PointLight* r) {}

void ProtectedMemoryVisitor::VisitNode(Node* r) {
  if (!r->children().empty()) {
    for (auto& child : r->children()) {
      child->Accept(this);
    }
  }
  VisitResource(r);
}

void ProtectedMemoryVisitor::VisitResource(Resource* r) {}

}  // namespace gfx
}  // namespace scenic_impl
