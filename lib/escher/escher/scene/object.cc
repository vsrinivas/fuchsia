// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/object.h"

namespace escher {

Object::Object(const Shape& shape, const Material* material)
    : shape_(shape), material_(material) {
  // TODO: add support for materials.
  FTL_DCHECK(!material);
}

Object::~Object() {}

}  // namespace escher
