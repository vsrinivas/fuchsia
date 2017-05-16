// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/services/composer/types.fidl.h"
#include "lib/escher/escher/geometry/transform.h"

namespace mozart {
namespace composer {

inline escher::vec2 Unwrap(const mozart2::vec2Ptr& args) {
  return {args->x, args->y};
}

inline escher::vec3 Unwrap(const mozart2::vec3Ptr& args) {
  return {args->x, args->y, args->z};
}

inline escher::vec4 Unwrap(const mozart2::vec4Ptr& args) {
  return {args->x, args->y, args->z, args->w};
}

inline escher::quat Unwrap(const mozart2::QuaternionPtr& args) {
  return {args->x, args->y, args->z, args->w};
}

inline escher::Transform Unwrap(const mozart2::TransformPtr& args) {
  return {Unwrap(args->translation), Unwrap(args->scale),
          Unwrap(args->rotation), Unwrap(args->anchor)};
}

}  // namespace composer
}  // namespace mozart
