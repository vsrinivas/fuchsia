// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_INSTRUMENTATION_ASAN_ASAN_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_INSTRUMENTATION_ASAN_ASAN_INTERNAL_H_

#include <lib/instrumentation/asan.h>
#include <zircon/types.h>

#include <arch/kernel_aspace.h>

#ifdef __x86_64__

inline constexpr size_t kAsanShift = 3;
inline constexpr size_t kAsanShadowSize = 64UL * 1024UL * 1024UL * 1024UL;

static_assert(X86_KERNEL_KASAN_PDP_ENTRIES * 1024ul * 1024ul * 1024ul == kAsanShadowSize);
static_assert(KERNEL_ASPACE_SIZE >> kAsanShift == kAsanShadowSize);

#endif  // __x86_64__

#endif  // ZIRCON_KERNEL_LIB_INSTRUMENTATION_ASAN_ASAN_INTERNAL_H_
