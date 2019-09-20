// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/lights/ambient_light.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo AmbientLight::kTypeInfo = {
    ResourceType::kLight | ResourceType::kAmbientLight, "AmbientLight"};

AmbientLight::AmbientLight(Session* session, SessionId session_id, ResourceId id)
    : Light(session, session_id, id, AmbientLight::kTypeInfo) {}

}  // namespace gfx
}  // namespace scenic_impl
