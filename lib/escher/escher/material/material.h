// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/forward_declarations.h"
#include "escher/geometry/types.h"
#include "escher/renderer/texture.h"

namespace escher {

class Material : public ftl::RefCountedThreadSafe<Material> {
 public:
  explicit Material(TexturePtr texture = nullptr);
  ~Material();

  const TexturePtr& texture() const { return texture_; }
  vk::ImageView image_view() const { return image_view_; }
  vk::Sampler sampler() const { return sampler_; }
  mat2 texture_matrix() const { return texture_matrix_; }
  vec3 color() const { return color_; }

  void set_color(vec3 color) { color_ = color; }

 protected:
  TexturePtr texture_;
  // Cache image_view_ and sampler_ from texture_ so that we don't need an
  // indirection each time that we render using the material.
  vk::ImageView image_view_;
  vk::Sampler sampler_;

  // Matrix used to transform a shape's UV coordinates.
  mat2 texture_matrix_;
  vec3 color_;
};

typedef ftl::RefPtr<Material> MaterialPtr;

}  // namespace escher
