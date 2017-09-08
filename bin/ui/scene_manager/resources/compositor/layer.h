// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/resources/resource.h"

#include "escher/geometry/types.h"

namespace scene_manager {

class Layer;
class LayerStack;
class Renderer;
class Scene;
using LayerPtr = ftl::RefPtr<Layer>;
using RendererPtr = ftl::RefPtr<Renderer>;

// A Layer can appear in a LayerStack, and be displayed by a Compositor.
// TODO(MZ-249): Layers can currently only use a rendered scene as content, but
// should also be able to directly use an Image/ImagePipe.
class Layer : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Layer(Session* session, scenic::ResourceId id);

  ~Layer() override;

  // | Resource |
  void Accept(class ResourceVisitor* visitor) override;

  // SetRendererOp.
  bool SetRenderer(RendererPtr renderer);
  const RendererPtr& renderer() const { return renderer_; }

  // SetSizeOp.
  bool SetSize(const escher::vec2& size);
  const escher::vec2& size() const { return size_; }

  // SetColorOp.
  bool SetColor(const escher::vec4& color);
  const escher::vec4& color() const { return color_; }

  // |Resource|, DetachOp.
  bool Detach() override;

  // Add the scene rendered by this layer, if any, to |scenes_out|.
  void CollectScenes(std::set<Scene*>* scenes_out);

  bool IsDrawable() const;

  const escher::vec3& translation() const { return translation_; }
  uint32_t width() const { return static_cast<uint32_t>(size_.x); }
  uint32_t height() const { return static_cast<uint32_t>(size_.y); }

  // TODO(MZ-250): support detecting and/or setting layer opacity.
  bool opaque() const { return false; }

 private:
  friend class LayerStack;

  RendererPtr renderer_;
  escher::vec3 translation_ = escher::vec3(0, 0, 0);
  escher::vec2 size_ = escher::vec2(0, 0);
  escher::vec4 color_ = escher::vec4(1, 1, 1, 1);
  LayerStack* layer_stack_ = nullptr;
};

}  // namespace scene_manager
