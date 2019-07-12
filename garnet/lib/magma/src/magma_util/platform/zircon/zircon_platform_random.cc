// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "platform_random.h"

namespace magma {

// TODO(MA-537): remove this
void GetSecureRandomBytes(void* buffer, size_t size) { zx_cprng_draw(buffer, size); }
}  // namespace magma

void magma_platform_GetSecureRandomBytes(void* buffer, uint64_t size) {
  zx_cprng_draw(buffer, size);
}
