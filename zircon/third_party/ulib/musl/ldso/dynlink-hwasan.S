// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <asm.h>

#include "sanitizer-stubs.h"
#include "hwasan-stubs.h"

// See the end of dynlink.c for what this is about.

#if __has_feature(hwaddress_sanitizer)
#define HWASAN_STUB(name) sanitizer_stub __hwasan_##name;
HWASAN_STUBS
#endif  // __has_feature(hwaddress_sanitizer)
