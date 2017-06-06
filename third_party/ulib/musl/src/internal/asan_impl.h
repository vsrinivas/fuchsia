// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <string.h>

// These are aliases for the functions defined in libc, which are always
// the unsanitized versions.  The sanitizer runtimes can call them by these
// aliases when they are overriding libc's definitions of the unadorned
// symbols.  Though these names are not declared in any public header, they
// are always exported so that the libc ABI is uniform across sanitized and
// unsanitized builds (only unsanitized shared library binaries are used at
// link time, including linking the sanitizer runtime shared libraries).
//
// NOTE: userboot includes memcpy, memmove, and memset source files
// directly, so it needs to be able to handle their #include's of this
// header.
//
__typeof(memcpy) __unsanitized_memcpy;
__typeof(memmove) __unsanitized_memmove;
__typeof(memset) __unsanitized_memset;

#if __has_feature(address_sanitizer)

// In the sanitized build, the __asan_mem* names provided by the
// sanitizer runtime must have weak definitions in libc to satisfy
// its own references before the sanitizer runtime is loaded.
#define __asan_weak_alias(name) \
    __typeof(name) __asan_##name __attribute__((weak, alias(#name)));

#include <sanitizer/asan_interface.h>

void __asan_early_init(void) __attribute__((visibility("hidden")));

#else  // !__has_feature(address_sanitizer)

#define __asan_weak_alias(name) // Do nothing in unsanitized build.

#endif  // __has_feature(address_sanitizer)
