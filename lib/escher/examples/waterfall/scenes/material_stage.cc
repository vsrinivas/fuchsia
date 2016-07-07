// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/waterfall/scenes/material_stage.h"

namespace {

// Material design places objects from 0.0f to 24.0f. We inflate that range
// slightly to avoid clipping at the edges of the viewing volume.
constexpr float kNear = 25.0f;
constexpr float kFar = -1.0f;

}  // namespace

void InitStageForMaterial(escher::Stage* stage) {
  stage->set_viewing_volume(
      escher::ViewingVolume(0.0f, 0.0f, kNear, kFar));
  stage->set_key_light(escher::DirectionalLight(
      glm::vec2(M_PI / 2.0, M_PI / 4.0), M_PI / 4.0, 0.5));
}
