// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#define SHIFT_4K        (12)
#define SHIFT_16K       (14)
#define SHIFT_64K       (16)

/* arm specific stuff */
#ifdef ARM64_LARGE_PAGESIZE_64K
#define PAGE_SIZE_SHIFT (SHIFT_64K)
#elif ARM64_LARGE_PAGESIZE_16K
#define PAGE_SIZE_SHIFT (SHIFT_16K)
#else
#define PAGE_SIZE_SHIFT (SHIFT_4K)
#endif
#define USER_PAGE_SIZE_SHIFT SHIFT_4K

#define PAGE_SIZE (1L << PAGE_SIZE_SHIFT)
#define USER_PAGE_SIZE (1L << USER_PAGE_SIZE_SHIFT)

#if ARM64_CPU_CORTEX_A53
#define CACHE_LINE 64
#elif ARM64_CPU_CORTEX_A57
#define CACHE_LINE 64
#elif ARM64_CPU_CORTEX_A72
#define CACHE_LINE 64
#elif ARM64_CPU_CORTEX_A73
#define CACHE_LINE 64
#else
#error "define CACHE_LINE for the specific core"
#endif

#define MAX_CACHE_LINE 64

#ifndef ASSEMBLY
#define BM(base, count, val) (((val) & ((1UL << (count)) - 1)) << (base))
#else
#define BM(base, count, val) (((val) & ((0x1 << (count)) - 1)) << (base))
#endif

#define ARM64_MMFR0_ASIDBITS_16     BM(4,4,2)
#define ARM64_MMFR0_ASIDBITS_8      BM(4,4,0)
#define ARM64_MMFR0_ASIDBITS_MASK   BM(4,4,15)

#define ARCH_DEFAULT_STACK_SIZE 8192

