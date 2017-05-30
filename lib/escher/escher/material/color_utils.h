// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/geometry/types.h"

using escher::vec3;

namespace escher {

vec3 LinearToSrgb(vec3 linear_color);

vec3 SrgbToLinear(vec3 srgb_color);

vec3 HsvToLinear(vec3 hsv_color);

}  // namespace escher
