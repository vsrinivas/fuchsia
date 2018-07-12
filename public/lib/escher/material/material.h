// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_MATERIAL_MATERIAL_H_
#define LIB_ESCHER_MATERIAL_MATERIAL_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/vk/texture.h"
#include "lib/fxl/memory/ref_counted.h"

namespace escher {

class Material;
using MaterialPtr = fxl::RefPtr<Material>;

class Material : public fxl::RefCountedThreadSafe<Material> {
 public:
  explicit Material();
  ~Material();

  static MaterialPtr New(vec4 color, TexturePtr texture = TexturePtr());

  const TexturePtr& texture() const { return texture_; }
  vk::ImageView vk_image_view() const { return image_view_; }
  vk::Sampler vk_sampler() const { return sampler_; }
  const vec4& color() const { return color_; }

  void set_color(vec4 color) { color_ = color; }
  void set_color(vec3 color) { color_ = vec4(color, 1); }
  void SetTexture(TexturePtr texture);

  bool opaque() const { return opaque_; }
  void set_opaque(bool opaque) { opaque_ = opaque; }

 protected:
  TexturePtr texture_;
  // Cache image_view_ and sampler_ from texture_ so that we don't need an
  // indirection each time that we render using the material.
  vk::ImageView image_view_;
  vk::Sampler sampler_;

  vec4 color_ = vec4(1, 1, 1, 1);
  bool opaque_ = true;
};

}  // namespace escher

#endif  // LIB_ESCHER_MATERIAL_MATERIAL_H_
