// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_RENDERERS_RENDERER_H_
#define GARNET_LIB_UI_GFX_RESOURCES_RENDERERS_RENDERER_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include "garnet/lib/ui/gfx/resources/resource.h"
#include "garnet/lib/ui/gfx/resources/resource_visitor.h"
#include "src/ui/lib/escher/scene/object.h"

namespace escher {
class BatchGpuUploader;
}  // namespace escher

namespace scenic_impl {
namespace gfx {

class Camera;
class Node;
class Scene;
using CameraPtr = fxl::RefPtr<Camera>;
using ScenePtr = fxl::RefPtr<Scene>;

// Placeholder Renderer. Doesn't deal with framerate, framebuffer, etc. yet.
class Renderer : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Renderer(Session* session, ResourceId id);
  ~Renderer();

  std::vector<escher::Object> CreateDisplayList(
      const ScenePtr& scene, escher::vec2 screen_dimensions,
      escher::BatchGpuUploader* uploader);

  // |Resource|
  void Accept(class ResourceVisitor* visitor) override;

  // Nothing will be rendered unless a camera has been set, and the camera
  // points at a scene.
  void SetCamera(CameraPtr camera);

  // Set the shadow algorithm that the |Renderer| should use when lighting
  // the scene.
  bool SetShadowTechnique(::fuchsia::ui::gfx::ShadowTechnique technique);

  // Set whether clipping is disabled; false by default.
  void DisableClipping(bool disable_clipping);

  Camera* camera() const { return camera_.get(); }

  ::fuchsia::ui::gfx::ShadowTechnique shadow_technique() const {
    return shadow_technique_;
  }

  void set_enable_debugging(bool enable) { enable_debugging_ = enable; }
  bool enable_debugging() const { return enable_debugging_; }

 private:
  // Context for a Visitor.
  // The VisitorContext is only valid during a Visitor pass, and should not be
  // accessed outside of that.
  struct VisitorContext {
    VisitorContext() {}
    VisitorContext(const escher::MaterialPtr& default_material, float opacity,
                   bool disable_clipping, escher::BatchGpuUploader* uploader)
        : default_material(default_material),
          opacity(opacity),
          disable_clipping(disable_clipping),
          batch_gpu_uploader(uploader) {}
    VisitorContext(const VisitorContext& other_context)
        : default_material(other_context.default_material),
          opacity(other_context.opacity),
          disable_clipping(other_context.disable_clipping),
          batch_gpu_uploader(other_context.batch_gpu_uploader) {}

    const escher::MaterialPtr default_material;
    // Opacity needs to be separate from default material since default material
    // is null for geometry that can serve as clippers.
    float opacity = 1.0f;
    const bool disable_clipping = false;
    // Client managing the VisitorContext must guarantee this to be valid for
    // the lifetime of the Visitor.
    escher::BatchGpuUploader* batch_gpu_uploader;
  };

  class Visitor : public ResourceVisitor {
   public:
    explicit Visitor(Renderer::VisitorContext context);

    std::vector<escher::Object> TakeDisplayList();

    void Visit(Memory* r) override;
    void Visit(Image* r) override;
    void Visit(ImagePipe* r) override;
    void Visit(Buffer* r) override;
    void Visit(View* r) override;
    void Visit(ViewNode* r) override;
    void Visit(ViewHolder* r) override;
    void Visit(EntityNode* r) override;
    void Visit(OpacityNode* r) override;
    void Visit(ShapeNode* r) override;
    void Visit(CircleShape* r) override;
    void Visit(RectangleShape* r) override;
    void Visit(RoundedRectangleShape* r) override;
    void Visit(MeshShape* r) override;
    void Visit(Material* r) override;
    void Visit(Compositor* r) override;
    void Visit(DisplayCompositor* r) override;
    void Visit(LayerStack* r) override;
    void Visit(Layer* r) override;
    void Visit(Scene* r) override;
    void Visit(Camera* r) override;
    void Visit(Renderer* r) override;
    void Visit(Light* r) override;
    void Visit(AmbientLight* r) override;
    void Visit(DirectionalLight* r) override;
    void Visit(PointLight* r) override;
    void Visit(Import* r) override;

   private:
    // Visits a node and all it's children.
    void VisitNode(Node* r);
    // Called by |VisitNode|. Determines if a node and its children are clipped,
    // and generates clipped geometry if so.
    void VisitAndMaybeClipNode(Node* r);
    std::vector<escher::Object> GenerateClippeeDisplayList(Node* r);
    std::vector<escher::Object> GenerateClipperDisplayList(Node* r);

    std::vector<escher::Object> display_list_;
    Renderer::VisitorContext context_;
  };

  CameraPtr camera_;
  escher::MaterialPtr default_material_;
  ::fuchsia::ui::gfx::ShadowTechnique shadow_technique_ =
      ::fuchsia::ui::gfx::ShadowTechnique::UNSHADOWED;
  bool disable_clipping_ = false;
  bool enable_debugging_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(Renderer);
};

using RendererPtr = fxl::RefPtr<Renderer>;

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_RENDERERS_RENDERER_H_
