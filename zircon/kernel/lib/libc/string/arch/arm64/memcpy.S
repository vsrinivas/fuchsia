// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The memcpy implementation is verbatim from cortex-strings.
// We include it here so we can define aliases for the sanitizer runtime.

#include "third_party/lib/cortex-strings/src/aarch64/memcpy.S"

.weak memcpy

.globl __unsanitized_memcpy
.hidden __unsanitized_memcpy
__unsanitized_memcpy = memcpy
