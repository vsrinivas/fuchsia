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

// See dynlink.c for the full explanation.  The compiler generates calls to
// these implicitly.  They are PLT calls into the ASan runtime, which is fine
// in and of itself at this point (unlike in dynlink.c).  But they might also
// use ShadowCallStack, which is not set up yet.  So make sure references here
// only use the libc-internal symbols, which don't have any setup requirements.
#define __asan_weak_ref(name) __asm__(".weakref __asan_" name ",__libc_" name);

#include <sanitizer/asan_interface.h>

void __asan_early_init(void) __attribute__((visibility("hidden")));
#define ADDR_MASK UINTPTR_MAX

static inline void __hwasan_init(void) {}

#elif __has_feature(hwaddress_sanitizer)

// Expose the hwasan interface.
#include <sanitizer/hwasan_interface.h>

void __asan_early_init(void) __attribute__((visibility("hidden")));

// In the sanitized build, the __hwasan_mem* names provided by the
// sanitizer runtime must have weak definitions in libc to satisfy
// its own references before the sanitizer runtime is loaded.
#define __asan_weak_alias(name) __typeof(name) __hwasan_##name __attribute__((weak, alias(#name)));
#define __asan_weak_ref(name) __asm__(".weakref __hwasan_" name ",__libc_" name);

// With ARM TBI, the bottom 56 bits are the relevant addressing bits.
#define ADDR_MASK (~(UINT64_C(0xFF) << 56))

// This is explicitly called in __libc_start_main.c before extensions are initialized.
void __hwasan_init(void);

#else  // !__has_feature(address_sanitizer)

#define __asan_weak_alias(name)  // Do nothing in unsanitized build.
#define __asan_weak_ref(name)

// Allow this to be an empty inline for non-sanitized cases so we don't need to
// stick `!__has_feature(address_sanitizer) && !__has_feature(hwaddress_sanitizer)`
// in a bunch of places.
static inline void __asan_early_init(void) {}
#define ADDR_MASK UINTPTR_MAX

static inline void __hwasan_init(void) {}

#endif  // __has_feature(address_sanitizer)
