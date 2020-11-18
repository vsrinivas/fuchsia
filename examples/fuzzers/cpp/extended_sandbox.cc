// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "src/lib/files/directory.h"

// A simple fuzzer that needs the non-default `isolated-cache-storage` sandbox feature to run
// correctly.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  assert(files::IsDirectory("/cache"));
  return 0;
}
