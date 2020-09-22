// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_LAYER_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_LAYER_H_

#include <memory>
#include <set>

#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/scene/viewing_volume.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"

namespace scenic_impl {
namespace gfx {

class Layer;
class LayerStack;
class Renderer;
class Scene;
using LayerPtr = fxl::RefPtr<Layer>;
using RendererPtr = fxl::RefPtr<Renderer>;

// A Layer can appear in a LayerStack, and be displayed by a Compositor.
// TODO(fxbug.dev/23495): Layers can currently only use a rendered scene as content, but
// should also be able to directly use an Image/ImagePipe.
class Layer : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Layer(Session* session, SessionId session_id, ResourceId id);

  ~Layer() override;

  // | Resource |
  void Accept(class ResourceVisitor* visitor) override;

  // SetRendererCmd.
  bool SetRenderer(RendererPtr renderer);
  const RendererPtr& renderer() const { return renderer_; }

  // SetSizeCmd.
  bool SetSize(const escher::vec2& size, ErrorReporter* reporter);
  const escher::vec2& size() const { return size_; }

  // SetColorCmd.
  bool SetColor(const escher::vec4& color);
  const escher::vec4& color() const { return color_; }

  // |Resource|, DetachCmd.
  bool Detach(ErrorReporter* reporter) override;

  // Return the scene rendered by this layer, if any.
  fxl::WeakPtr<Scene> scene();

  // Add the scene rendered by this layer, if any, to |scenes_out|.
  void CollectScenes(std::set<Scene*>* scenes_out);

  bool IsDrawable() const;

  const escher::vec3& translation() const { return translation_; }
  uint32_t width() const { return static_cast<uint32_t>(size_.x); }
  uint32_t height() const { return static_cast<uint32_t>(size_.y); }

  // TODO(fxbug.dev/23496): support detecting and/or setting layer opacity.
  bool opaque() const { return false; }

  // Returns the current viewing volume of the layer. Used by the compositor
  // when initializing the stage, as well as for hit testing.
  escher::ViewingVolume GetViewingVolume() const;

  // Returns the transform from screen space coordinates to world space.
  // It maps from pixel to camera space, and then undoes the camera's transformation matrices.
  // Returns std::nullopt if either the renderer or the camera has not been set.
  std::optional<escher::mat4> GetWorldFromScreenTransform() const;

 private:
  friend class LayerStack;

  RendererPtr renderer_;
  escher::vec3 translation_ = escher::vec3(0, 0, 0);
  escher::vec2 size_ = escher::vec2(0, 0);
  escher::vec4 color_ = escher::vec4(1, 1, 1, 1);
  LayerStack* layer_stack_ = nullptr;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_LAYER_H_
