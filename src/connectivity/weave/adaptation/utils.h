// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <sys/types.h>

using memset_fn_t = void* (*)(void *, int, size_t);

static volatile memset_fn_t memset_fn = memset;

// secure_memset is similar to memset except that it won't be optimized away.
// Refer to https://en.cppreference.com/w/c/string/byte/memset.
void* secure_memset(void *ptr, int c, size_t len) {
  return memset_fn(ptr, c, len);
}
