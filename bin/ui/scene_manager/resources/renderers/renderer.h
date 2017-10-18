// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/resources/resource.h"
#include "garnet/bin/ui/scene_manager/resources/resource_visitor.h"
#include "lib/escher/scene/object.h"
#include "lib/ui/scenic/fidl/renderer.fidl.h"

namespace scene_manager {

class Camera;
class Scene;
using CameraPtr = fxl::RefPtr<Camera>;
using ScenePtr = fxl::RefPtr<Scene>;

// Placeholder Renderer. Doesn't deal with framerate, framebuffer, etc. yet.
class Renderer : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // Any swapchain that uses PaperRenderer must be a multiple of this many
  // pixels.
  static const uint32_t kRequiredSwapchainPixelMultiple;

  Renderer(Session* session, scenic::ResourceId id);
  ~Renderer();

  std::vector<escher::Object> CreateDisplayList(const ScenePtr& scene,
                                                escher::vec2 screen_dimensions);

  // |Resource|
  void Accept(class ResourceVisitor* visitor) override;

  // Nothing will be rendered unless a camera has been set, and the camera
  // points at a scene.
  void SetCamera(CameraPtr camera);

  // Set the shadow algorithm that the |Renderer| should use when lighting
  // the scene.
  bool SetShadowTechnique(scenic::ShadowTechnique technique);

  // Set whether clipping is disabled; false by default.
  void DisableClipping(bool disable_clipping);

  Camera* camera() const { return camera_.get(); }

 private:
  class Visitor : public ResourceVisitor {
   public:
    std::vector<escher::Object> TakeDisplayList();

    void Visit(GpuMemory* r) override;
    void Visit(HostMemory* r) override;
    void Visit(Image* r) override;
    void Visit(ImagePipe* r) override;
    void Visit(Buffer* r) override;
    void Visit(EntityNode* r) override;
    void Visit(ShapeNode* r) override;
    void Visit(CircleShape* r) override;
    void Visit(RectangleShape* r) override;
    void Visit(RoundedRectangleShape* r) override;
    void Visit(MeshShape* r) override;
    void Visit(Material* r) override;
    void Visit(DisplayCompositor* r) override;
    void Visit(LayerStack* r) override;
    void Visit(Layer* r) override;
    void Visit(Scene* r) override;
    void Visit(Camera* r) override;
    void Visit(Renderer* r) override;
    void Visit(DirectionalLight* r) override;
    void Visit(Import* r) override;

   protected:
   private:
    friend class Renderer;
    Visitor(const escher::MaterialPtr& default_material, bool disable_clipping);

    void VisitNode(Node* r);

    std::vector<escher::Object> display_list_;
    const escher::MaterialPtr& default_material_;
    const bool disable_clipping_;
  };

  CameraPtr camera_;
  escher::MaterialPtr default_material_;
  scenic::ShadowTechnique shadow_technique_ =
      scenic::ShadowTechnique::SCREEN_SPACE;
  bool disable_clipping_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(Renderer);
};

using RendererPtr = fxl::RefPtr<Renderer>;

}  // namespace scene_manager
