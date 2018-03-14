// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/lights/ambient_light.h"

namespace scenic {
namespace gfx {

const ResourceTypeInfo AmbientLight::kTypeInfo = {
    ResourceType::kLight | ResourceType::kAmbientLight, "AmbientLight"};

AmbientLight::AmbientLight(Session* session, scenic::ResourceId id)
    : Light(session, id, AmbientLight::kTypeInfo) {}

}  // namespace gfx
}  // namespace scenic
