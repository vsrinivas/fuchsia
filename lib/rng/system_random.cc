// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/rng/system_random.h"

#include <zircon/syscalls.h>

namespace rng {

void SystemRandom::InternalDraw(void* buffer, size_t buffer_size) {
  zx_cprng_draw(buffer, buffer_size);
}

}  // namespace rng
