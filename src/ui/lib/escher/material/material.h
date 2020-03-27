// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_MATERIAL_MATERIAL_H_
#define SRC_UI_LIB_ESCHER_MATERIAL_MATERIAL_H_

#include "src/lib/fxl/memory/ref_counted.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/vk/texture.h"

#include <vulkan/vulkan.hpp>

namespace escher {

class Material;
using MaterialPtr = fxl::RefPtr<Material>;

class Material : public fxl::RefCountedThreadSafe<Material> {
 public:
  enum class Type { kOpaque, kTranslucent, kWireframe };

  explicit Material();
  ~Material();

  static MaterialPtr New(vec4 color, TexturePtr texture = TexturePtr());

  const TexturePtr& texture() const { return texture_; }
  vk::ImageView vk_image_view() const { return image_view_; }
  vk::Sampler vk_sampler() const { return sampler_; }
  const vec4& color() const { return color_; }
  const vec4 GetPremultipliedRgba() const { return vec4(vec3(color_) * color_.a, color_.a); }

  void set_color(vec4 color) { color_ = color; }
  void set_color(vec3 color) { color_ = vec4(color, 1); }
  void SetTexture(TexturePtr texture);

  Type type() const { return type_; }
  void set_type(Type type) { type_ = type; }
  bool opaque() const { return type_ == Type::kOpaque; }

 protected:
  TexturePtr texture_;
  // Cache image_view_ and sampler_ from texture_ so that we don't need an
  // indirection each time that we render using the material.
  vk::ImageView image_view_;
  vk::Sampler sampler_;

  vec4 color_ = vec4(1, 1, 1, 1);
  Type type_ = Type::kOpaque;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_MATERIAL_MATERIAL_H_
