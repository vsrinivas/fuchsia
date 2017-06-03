// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/mtl/tasks/message_loop.h"

#include "apps/mozart/src/scene/resources/resource_visitor.h"

#include "escher/scene/object.h"

namespace mozart {
namespace scene {

// Placeholder Renderer. Doesn't deal with framerate, framebuffer, etc. yet.
class Renderer {
 public:
  Renderer();
  ~Renderer();

  std::vector<escher::Object> CreateDisplayList(Node* root_node,
                                                escher::vec2 screen_dimensions);

 private:
  class Visitor : public ResourceVisitor {
   public:
    std::vector<escher::Object> TakeDisplayList();

    void Visit(GpuMemory* r) override;
    void Visit(HostMemory* r) override;
    void Visit(Image* r) override;
    void Visit(EntityNode* r) override;
    void Visit(Node* r) override;
    void Visit(ShapeNode* r) override;
    void Visit(CircleShape* r) override;
    void Visit(Shape* r) override;
    void Visit(Link* r) override;
    void Visit(Material* r) override;

   private:
    std::vector<escher::Object> display_list_;
  };
};

}  // namespace scene
}  // namespace mozart
