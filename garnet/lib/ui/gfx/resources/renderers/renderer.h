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
