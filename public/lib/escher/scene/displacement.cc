// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/scene/displacement.h"

namespace escher {

Displacement::Displacement() {}

Displacement::~Displacement() {}

Displacement Displacement::MakeWave(const vec2& start, const vec2& end,
                                    float max, float theta_min,
                                    float theta_max) {
  Displacement displacement;
  displacement.type_ = Type::kWave;
  displacement.parameters_ = vec4(start, end);
  displacement.max_ = max;
  displacement.theta_min_ = theta_min;
  displacement.theta_max_ = theta_max;
  return displacement;
}

}  // namespace escher
