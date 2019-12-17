// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/macros.h"
#include "platform_object.h"

namespace magma {

bool PlatformObject::IdFromHandle(uint32_t handle, uint64_t* id_out) {
  return DRETF(false, "Not implemented");
}

}  // namespace magma
