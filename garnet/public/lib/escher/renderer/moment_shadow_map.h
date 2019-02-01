// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_MOMENT_SHADOW_MAP_H_
#define LIB_ESCHER_RENDERER_MOMENT_SHADOW_MAP_H_

#include "lib/escher/renderer/shadow_map.h"

namespace escher {

// A |MomentShadowMap| is a special |ShadowMap| that encodes 4 moments of depth
// in the texture. In addition, it uses a different image format from the normal
// shadow map. See also http://momentsingraphics.de/?page_id=51
class MomentShadowMap final : public ShadowMap {
 public:
  static const ShadowMapTypeInfo kTypeInfo;
  const TypeInfo& type_info() const override { return kTypeInfo; }

 protected:
  MomentShadowMap(const ImagePtr& image, const mat4& matrix,
                  const vec3& light_color);

 private:
  friend class ShadowMapRenderer;

  FRIEND_REF_COUNTED_THREAD_SAFE(MomentShadowMap);
  FXL_DISALLOW_COPY_AND_ASSIGN(MomentShadowMap);
};

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_MOMENT_SHADOW_MAP_H_
