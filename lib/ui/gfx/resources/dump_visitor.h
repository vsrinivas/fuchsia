// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_DUMP_VISITOR_H_
#define GARNET_LIB_UI_GFX_RESOURCES_DUMP_VISITOR_H_

#include <cstdint>
#include <iosfwd>

#include "lib/escher/vk/image.h"

#include "garnet/lib/ui/gfx/resources/resource_visitor.h"

namespace scenic {
namespace gfx {

class Node;
class Resource;

// Dumps information about resources to an output stream.
// The output stream must remain in scope until the visitor is destroyed.
class DumpVisitor : public ResourceVisitor {
 public:
  DumpVisitor(std::ostream& output);
  ~DumpVisitor();

  void Visit(GpuMemory* r) override;
  void Visit(HostMemory* r) override;
  void Visit(Image* r) override;
  void Visit(ImagePipe* r) override;
  void Visit(Buffer* r) override;
  void Visit(View* r) override;
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
  void Visit(DisplayCompositor* r) override;
  void Visit(LayerStack* r) override;
  void Visit(Layer* r) override;
  void Visit(Camera* r) override;
  void Visit(Renderer* r) override;
  void Visit(Light* r) override;
  void Visit(AmbientLight* r) override;
  void Visit(DirectionalLight* r) override;
  void Visit(Import* r) override;

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

  std::ostream& output_;
  bool partial_line_ = false;
  uint32_t property_count_ = 0u;
  uint32_t indentation_ = 0u;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_DUMP_VISITOR_H_
