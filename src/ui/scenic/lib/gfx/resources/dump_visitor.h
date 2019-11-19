// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_DUMP_VISITOR_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_DUMP_VISITOR_H_

#include <cstdint>
#include <iosfwd>
#include <unordered_set>

#include "src/ui/lib/escher/vk/image.h"
#include "src/ui/scenic/lib/gfx/id.h"
#include "src/ui/scenic/lib/gfx/resources/resource_visitor.h"

namespace scenic_impl {
namespace gfx {

class Node;
class Resource;

// Dumps information about resources to an output stream.
class DumpVisitor : public ResourceVisitor {
 public:
  // Context for a DumpVisitor.
  // The VisitorContext is only valid during a DumpVisitor pass, and should not
  // be accessed outside of that.
  struct VisitorContext {
    VisitorContext(std::ostream& out, std::unordered_set<GlobalId, GlobalId::Hash>* visited_list)
        : output(out), visited(visited_list) {}
    VisitorContext(const VisitorContext&& other) : output(other.output), visited(other.visited) {}

    std::ostream& output;
    std::unordered_set<GlobalId, GlobalId::Hash>* visited;
  };

  DumpVisitor(VisitorContext context);
  virtual ~DumpVisitor() = default;

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

 private:
  void VisitNode(Node* r);
  void VisitResource(Resource* r);
  void VisitEscherImage(escher::Image* i);

  void BeginItem(const char* type, Resource* r);
  std::ostream& WriteProperty(const char* label);
  void EndItem();

  void BeginSection(const char* label);
  void EndSection();

  void BeginLine();
  void EndLine();

  VisitorContext context_;

  bool partial_line_ = false;
  uint32_t property_count_ = 0u;
  uint32_t indentation_ = 0u;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_DUMP_VISITOR_H_
