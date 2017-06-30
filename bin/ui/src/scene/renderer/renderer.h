// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// TODO(MZ-148): now that Renderers are Resources, they should be moved to
// apps/mozart/src/scene/resources/renderers.

#include "lib/mtl/tasks/message_loop.h"

#include "apps/mozart/src/scene/resources/resource.h"
#include "apps/mozart/src/scene/resources/resource_visitor.h"

#include "escher/scene/object.h"

namespace mozart {
namespace scene {

class Camera;
class FrameScheduler;
class Scene;
using CameraPtr = ftl::RefPtr<Camera>;
using ScenePtr = ftl::RefPtr<Scene>;

// Placeholder Renderer. Doesn't deal with framerate, framebuffer, etc. yet.
class Renderer : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  ~Renderer();

  std::vector<escher::Object> CreateDisplayList(const ScenePtr& scene,
                                                escher::vec2 screen_dimensions);

  // |Resource|
  void Accept(class ResourceVisitor* visitor) override;

  // Nothing will be rendered unless a camera has been set, and the camera
  // points at a scene.
  void SetCamera(CameraPtr camera);

  Camera* camera() const { return camera_.get(); }
  FrameScheduler* frame_scheduler() const { return frame_scheduler_; }

  virtual void DrawFrame() = 0;

 protected:
  // Renderer is a "leaf interface" of the Session API.  Even though it has
  // subclasses, these present exactly the same interface to callers, therefore
  // we don't waste valuable ResourceTypeInfo bits to distinguish them.
  Renderer(Session* session, ResourceId id, FrameScheduler* frame_scheduler);

 private:
  class Visitor : public ResourceVisitor {
   public:
    std::vector<escher::Object> TakeDisplayList();

    void Visit(GpuMemory* r) override;
    void Visit(HostMemory* r) override;
    void Visit(Image* r) override;
    void Visit(ImagePipe* r) override;
    void Visit(EntityNode* r) override;
    void Visit(ShapeNode* r) override;
    void Visit(CircleShape* r) override;
    void Visit(RectangleShape* r) override;
    void Visit(RoundedRectangleShape* r) override;
    void Visit(Material* r) override;
    void Visit(Scene* r) override;
    void Visit(Camera* r) override;
    void Visit(Renderer* r) override;
    void Visit(DirectionalLight* r) override;
    void Visit(Import* r) override;

   private:
    void VisitNode(Node* r);

    std::vector<escher::Object> display_list_;
  };

  FrameScheduler* const frame_scheduler_;
  CameraPtr camera_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Renderer);
};

using RendererPtr = ftl::RefPtr<Renderer>;

}  // namespace scene
}  // namespace mozart
