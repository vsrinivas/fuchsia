// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/sanitizer.h>

// NOTE: userboot includes memcpy, memmove, and memset source files
// directly, so it needs to be able to handle their #include's of this
// header.

#if __has_feature(address_sanitizer)

// In the sanitized build, the __asan_mem* names provided by the
// sanitizer runtime must have weak definitions in libc to satisfy
// its own references before the sanitizer runtime is loaded.
#define __asan_weak_alias(name) __typeof(name) __asan_##name __attribute__((weak, alias(#name)));

#include <sanitizer/asan_interface.h>

void __asan_early_init(void) __attribute__((visibility("hidden")));

#elif __has_feature(hwaddress_sanitizer)

// Expose the hwasan interface.
#include <sanitizer/hwasan_interface.h>

void __asan_early_init(void) __attribute__((visibility("hidden")));

// In the sanitized build, the __hwasan_mem* names provided by the
// sanitizer runtime must have weak definitions in libc to satisfy
// its own references before the sanitizer runtime is loaded.
#define __asan_weak_alias(name) __typeof(name) __hwasan_##name __attribute__((weak, alias(#name)));

#else  // !__has_feature(address_sanitizer)

#define __asan_weak_alias(name)  // Do nothing in unsanitized build.

// Allow this to be an empty inline for non-sanitized cases so we don't need to
// stick `!__has_feature(address_sanitizer) && !__has_feature(hwaddress_sanitizer)`
// in a bunch of places.
static inline void __asan_early_init(void) {}

#endif  // __has_feature(address_sanitizer)
