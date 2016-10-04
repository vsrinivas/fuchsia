// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/directional_light.h"

#include <utility>

namespace escher {

DirectionalLight::DirectionalLight() {}

DirectionalLight::DirectionalLight(vec2 direction,
                                   float dispersion,
                                   float intensity)
    : direction_(std::move(direction)),
      dispersion_(dispersion),
      intensity_(intensity) {}

DirectionalLight::~DirectionalLight() {}

}  // namespace escher
