// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_random.h"

#include <zircon/syscalls.h>

namespace magma {

void GetSecureRandomBytes(void* buffer, size_t size) { zx_cprng_draw(buffer, size); }
} // namespace magma
