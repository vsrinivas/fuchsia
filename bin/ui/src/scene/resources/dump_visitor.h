// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <iosfwd>

#include "apps/mozart/src/scene/resources/resource_visitor.h"

namespace mozart {
namespace scene {

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
  void Visit(EntityNode* r) override;
  void Visit(ShapeNode* r) override;
  void Visit(TagNode* r) override;
  void Visit(Scene* r) override;
  void Visit(CircleShape* r) override;
  void Visit(RectangleShape* r) override;
  void Visit(RoundedRectangleShape* r) override;
  void Visit(Material* r) override;
  void Visit(Camera* r) override;
  void Visit(Renderer* r) override;
  void Visit(DirectionalLight* r) override;
  void Visit(Import* r) override;

 private:
  void VisitNode(Node* r);
  void VisitResource(Resource* r);

  void BeginItem(const char* type, uint32_t resource_id = 0u);
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

}  // namespace scene
}  // namespace mozart
