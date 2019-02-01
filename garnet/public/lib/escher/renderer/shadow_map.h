// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_SHADOW_MAP_H_
#define LIB_ESCHER_RENDERER_SHADOW_MAP_H_

#include "lib/escher/base/typed_reffable.h"
#include "lib/escher/forward_declarations.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/renderer/renderer.h"
#include "lib/escher/renderer/shadow_map_type_info.h"
#include "lib/escher/vk/texture.h"

namespace escher {

class ShadowMap;
typedef fxl::RefPtr<ShadowMap> ShadowMapPtr;

// A ShadowMap encapsulates the texture that a shadow map has been renderered
// into, the matrix that should be used to sample from it, and the color of the
// associated light.
class ShadowMap : public TypedReffable<ShadowMapTypeInfo> {
 public:
  static const ShadowMapTypeInfo kTypeInfo;
  const TypeInfo& type_info() const override { return kTypeInfo; }

  const TexturePtr& texture() { return texture_; }
  const ImagePtr& image() { return texture_->image(); }

  // Return a matrix that can be used to transform world-space coordinates into
  // "shadow space", for sampling from the shadow map.
  const glm::mat4& matrix() { return matrix_; }

  // Return the color of the light that was used to produce the shadow map.
  const glm::vec3& light_color() { return light_color_; }

 protected:
  ShadowMap(ImagePtr image, glm::mat4 matrix, glm::vec3 light_color);
  ~ShadowMap() override;

 private:
  friend class ShadowMapRenderer;

  const TexturePtr texture_;
  const glm::mat4 matrix_;
  const glm::vec3 light_color_;

  FRIEND_REF_COUNTED_THREAD_SAFE(ShadowMap);
  FXL_DISALLOW_COPY_AND_ASSIGN(ShadowMap);
};

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_SHADOW_MAP_H_
