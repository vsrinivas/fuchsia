// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_LIGHTS_AMBIENT_LIGHT_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_LIGHTS_AMBIENT_LIGHT_H_

#include "src/ui/scenic/lib/gfx/resources/lights/light.h"

namespace scenic_impl {
namespace gfx {

class AmbientLight final : public Light {
 public:
  static const ResourceTypeInfo kTypeInfo;

  AmbientLight(Session* session, SessionId session_id, ResourceId id);

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_LIGHTS_AMBIENT_LIGHT_H_
