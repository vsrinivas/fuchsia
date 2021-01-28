// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_LIMITS_H_
#define SYSROOT_ZIRCON_LIMITS_H_

#include <stdint.h>

#define ZX_PAGE_SHIFT ((uint32_t)12u)
#define ZX_PAGE_SIZE ((uintptr_t)(1u << ZX_PAGE_SHIFT))
#define ZX_PAGE_MASK (ZX_PAGE_SIZE - 1u)

#if defined(__x86_64__) || defined(__i386__)

#define ZX_MIN_PAGE_SHIFT ((uint32_t)12u)
#define ZX_MAX_PAGE_SHIFT ((uint32_t)21u)

#elif defined(__aarch64__)

#define ZX_MIN_PAGE_SHIFT ((uint32_t)12u)
#define ZX_MAX_PAGE_SHIFT ((uint32_t)16u)

#else

#error what architecture?

#endif

#define ZX_MIN_PAGE_SIZE ((uintptr_t)(1u << ZX_MIN_PAGE_SHIFT))
#define ZX_MAX_PAGE_SIZE ((uintptr_t)(1u << ZX_MAX_PAGE_SHIFT))

#endif  // SYSROOT_ZIRCON_LIMITS_H_
