// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/resources/shapes/circle_shape.h"

namespace mozart {
namespace composer {

const ResourceTypeInfo CircleShape::kTypeInfo = {
    ResourceType::kShape | ResourceType::kCircle, "CircleShape"};

CircleShape::CircleShape(Session* session, float initial_radius)
    : Shape(session, CircleShape::kTypeInfo), radius_(initial_radius) {}

}  // namespace composer
}  // namespace mozart
