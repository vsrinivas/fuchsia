// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/scene/ambient_light.h"

namespace escher {

AmbientLight::AmbientLight(float intensity) : color_(vec3(intensity)) {}

AmbientLight::AmbientLight(const vec3& color) : color_(color) {}

AmbientLight::~AmbientLight() {}

}  // namespace escher
