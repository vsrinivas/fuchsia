// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_RESOURCES_LIGHTS_AMBIENT_LIGHT_H_
#define GARNET_LIB_UI_SCENIC_RESOURCES_LIGHTS_AMBIENT_LIGHT_H_

#include "garnet/lib/ui/scenic/resources/lights/light.h"

namespace scene_manager {

class AmbientLight final : public Light {
 public:
  static const ResourceTypeInfo kTypeInfo;

  AmbientLight(Session* session, scenic::ResourceId id);

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;
};

}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_RESOURCES_LIGHTS_AMBIENT_LIGHT_H_
