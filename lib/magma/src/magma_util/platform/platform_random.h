// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_RANDOM_H
#define PLATFORM_RANDOM_H

#include <cstddef>

namespace magma {

// Returns |size| random bytes at the location specified by |buffer|.
void GetSecureRandomBytes(void* buffer, size_t size);
} // namespace magma

#endif // PLATFORM_RANDOM_H
