// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/lights/light.h"

#include "src/ui/scenic/lib/scenic/util/error_reporter.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Light::kTypeInfo = {ResourceType::kLight, "Light"};

Light::Light(Session* session, SessionId session_id, ResourceId node_id,
             const ResourceTypeInfo& type_info)
    : Resource(session, session_id, node_id, type_info) {
  FX_DCHECK(type_info.IsKindOf(Light::kTypeInfo));
}

bool Light::SetColor(const glm::vec3& color) {
  color_ = color;
  return true;
}

}  // namespace gfx
}  // namespace scenic_impl
