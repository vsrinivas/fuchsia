// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_PROTECTED_MEMORY_VISITOR_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_PROTECTED_MEMORY_VISITOR_H_

#include "src/ui/scenic/lib/gfx/resources/resource_visitor.h"

namespace scenic_impl {
namespace gfx {

class Node;
class Resource;

// Traverses resources for content using protected memory.
class ProtectedMemoryVisitor : public ResourceVisitor {
 public:
  ProtectedMemoryVisitor() = default;
  virtual ~ProtectedMemoryVisitor() = default;

  void Visit(Memory* r) override;
  void Visit(Image* r) override;
  void Visit(ImagePipeBase* r) override;
  void Visit(Buffer* r) override;
  void Visit(View* r) override;
  void Visit(ViewNode* r) override;
  void Visit(ViewHolder* r) override;
  void Visit(EntityNode* r) override;
  void Visit(OpacityNode* r) override;
  void Visit(ShapeNode* r) override;
  void Visit(Scene* r) override;
  void Visit(CircleShape* r) override;
  void Visit(RectangleShape* r) override;
  void Visit(RoundedRectangleShape* r) override;
  void Visit(MeshShape* r) override;
  void Visit(Material* r) override;
  void Visit(Compositor* r) override;
  void Visit(DisplayCompositor* r) override;
  void Visit(LayerStack* r) override;
  void Visit(Layer* r) override;
  void Visit(Camera* r) override;
  void Visit(Renderer* r) override;
  void Visit(Light* r) override;
  void Visit(AmbientLight* r) override;
  void Visit(DirectionalLight* r) override;
  void Visit(PointLight* r) override;

  bool HasProtectedMemoryUse() const { return has_protected_memory_use_; }

 private:
  void VisitNode(Node* r);
  void VisitResource(Resource* r);

  bool has_protected_memory_use_ = false;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_PROTECTED_MEMORY_VISITOR_H_
