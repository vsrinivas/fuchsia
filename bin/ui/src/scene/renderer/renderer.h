// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/mtl/tasks/message_loop.h"

#include "apps/mozart/src/scene/resources/resource_visitor.h"

#include "escher/scene/object.h"

namespace mozart {
namespace scene {

class FrameScheduler;

// Placeholder Renderer. Doesn't deal with framerate, framebuffer, etc. yet.
class Renderer {
 public:
  explicit Renderer(FrameScheduler* frame_scheduler);
  ~Renderer();

  std::vector<escher::Object> CreateDisplayList(Node* root_node,
                                                escher::vec2 screen_dimensions);

  FrameScheduler* frame_scheduler() const { return frame_scheduler_; }

  Scene* scene() const { return scene_; }
  void set_scene(Scene* scene);

 private:
  class Visitor : public ResourceVisitor {
   public:
    std::vector<escher::Object> TakeDisplayList();

    void Visit(GpuMemory* r) override;
    void Visit(HostMemory* r) override;
    void Visit(Image* r) override;
    void Visit(EntityNode* r) override;
    void Visit(ShapeNode* r) override;
    void Visit(TagNode* r) override;
    void Visit(Scene* r) override;
    void Visit(CircleShape* r) override;
    void Visit(RectangleShape* r) override;
    void Visit(RoundedRectangleShape* r) override;
    void Visit(Material* r) override;
    void Visit(ProxyResource* r) override;

   private:
    void VisitNode(Node* r);

    std::vector<escher::Object> display_list_;
  };

  FrameScheduler* const frame_scheduler_;
  Scene* scene_ = nullptr;
};

}  // namespace scene
}  // namespace mozart
