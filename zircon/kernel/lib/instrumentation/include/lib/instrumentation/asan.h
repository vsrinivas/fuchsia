// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_INSTRUMENTATION_INCLUDE_LIB_INSTRUMENTATION_ASAN_H_
#define ZIRCON_KERNEL_LIB_INSTRUMENTATION_INCLUDE_LIB_INSTRUMENTATION_ASAN_H_

#include <arch/kernel_aspace.h>

#ifdef __x86_64__

#define X86_KERNEL_KASAN_PDP_ENTRIES (64)
#define KASAN_SHADOW_OFFSET (0xffffffe000000000UL)

#endif  // __x86_64__

#ifdef __clang__
#define NO_ASAN [[clang::no_sanitize("address")]]
#else
#define NO_ASAN __attribute__((no_sanitize_address))
#endif

#endif  // ZIRCON_KERNEL_LIB_INSTRUMENTATION_INCLUDE_LIB_INSTRUMENTATION_ASAN_H_
