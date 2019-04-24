// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_LAYER_H_
#define GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_LAYER_H_

#include <set>

#include "garnet/lib/ui/gfx/engine/hit.h"
#include "garnet/lib/ui/gfx/engine/hit_tester.h"
#include "garnet/lib/ui/gfx/resources/resource.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/scene/viewing_volume.h"

namespace scenic_impl {
namespace gfx {

class Layer;
class LayerStack;
class Renderer;
class Scene;
using LayerPtr = fxl::RefPtr<Layer>;
using RendererPtr = fxl::RefPtr<Renderer>;

// A Layer can appear in a LayerStack, and be displayed by a Compositor.
// TODO(SCN-249): Layers can currently only use a rendered scene as content, but
// should also be able to directly use an Image/ImagePipe.
class Layer : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Layer(Session* session, ResourceId id);

  ~Layer() override;

  // | Resource |
  void Accept(class ResourceVisitor* visitor) override;

  // SetRendererCmd.
  bool SetRenderer(RendererPtr renderer);
  const RendererPtr& renderer() const { return renderer_; }

  // SetSizeCmd.
  bool SetSize(const escher::vec2& size);
  const escher::vec2& size() const { return size_; }

  // SetColorCmd.
  bool SetColor(const escher::vec4& color);
  const escher::vec4& color() const { return color_; }

  // |Resource|, DetachCmd.
  bool Detach() override;

  // Add the scene rendered by this layer, if any, to |scenes_out|.
  void CollectScenes(std::set<Scene*>* scenes_out);

  bool IsDrawable() const;

  const escher::vec3& translation() const { return translation_; }
  uint32_t width() const { return static_cast<uint32_t>(size_.x); }
  uint32_t height() const { return static_cast<uint32_t>(size_.y); }

  // TODO(SCN-250): support detecting and/or setting layer opacity.
  bool opaque() const { return false; }

  // Performs a hit test into the scene of renderer, along the provided ray in
  // the layer's coordinate system.
  //
  // The hit collection behavior depends on the hit tester.
  std::vector<Hit> HitTest(const escher::ray4& ray,
                           HitTester* hit_tester) const;

  // Returns the current viewing volume of the layer. Used by the compositor
  // when initializing the stage, as well as for hit testing.
  escher::ViewingVolume GetViewingVolume() const;

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

#endif  // GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_LAYER_H_
