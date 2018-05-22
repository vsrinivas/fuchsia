// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_LIGHTS_LIGHT_H_
#define GARNET_LIB_UI_GFX_RESOURCES_LIGHTS_LIGHT_H_

#include "garnet/lib/ui/gfx/resources/resource.h"
#include "lib/escher/geometry/types.h"

namespace scenic {
namespace gfx {

class Light;
class Scene;
using LightPtr = fxl::RefPtr<Light>;

// Lights can be added to a Scene.
class Light : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  bool SetColor(const glm::vec3& color);
  const glm::vec3& color() const { return color_; }

 protected:
  Light(Session* session, scenic::ResourceId node_id,
        const ResourceTypeInfo& type_info);

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

 private:
  glm::vec3 color_ = {0.f, 0.f, 0.f};
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_LIGHTS_LIGHT_H_
