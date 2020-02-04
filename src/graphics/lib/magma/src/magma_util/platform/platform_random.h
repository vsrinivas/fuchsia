// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_RANDOM_H
#define PLATFORM_RANDOM_H

#include <stdint.h>

#include <cstddef>

namespace magma {

// Deprecated
// Returns |size| random bytes at the location specified by |buffer|.
void GetSecureRandomBytes(void* buffer, size_t size);
}  // namespace magma

#ifdef __cplusplus
extern "C" {
#endif

// Returns |size| random bytes at the location specified by |buffer|.
void magma_platform_GetSecureRandomBytes(void* buffer, uint64_t size);

#ifdef __cplusplus
}
#endif

#endif  // PLATFORM_RANDOM_H
