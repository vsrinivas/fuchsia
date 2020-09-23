// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"

// The memcpy implementation is verbatim from cortex-strings.
// But we also need to define aliases for the sanitizer runtime.

#include "third_party/lib/cortex-strings/src/aarch64/memcpy.S"

#ifdef HIDDEN
    .hidden memcpy
#else
ALIAS(memcpy, __unsanitized_memcpy)
ASAN_WEAK_ALIAS(memcpy)

// This name is called from inside libc to avoid going through the PLT.
.hidden __libc_memcpy
ALIAS(memcpy, __libc_memcpy)
#endif
