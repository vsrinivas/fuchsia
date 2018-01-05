// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#define SHIFT_4K (12)
#define SHIFT_16K (14)
#define SHIFT_64K (16)

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
#define PAGE_MASK (PAGE_SIZE - 1)

#define USER_PAGE_SIZE (1L << USER_PAGE_SIZE_SHIFT)
#define USER_PAGE_MASK (USER_PAGE_SIZE - 1)

/* the maximum cache line seen on any known ARM hardware */
#define MAX_CACHE_LINE 128

#ifndef __ASSEMBLER__
#define BM(base, count, val) (((val) & ((1UL << (count)) - 1)) << (base))
#else
#define BM(base, count, val) (((val) & ((0x1 << (count)) - 1)) << (base))
#endif

#define ARM64_MMFR0_ASIDBITS_16 BM(4, 4, 2)
#define ARM64_MMFR0_ASIDBITS_8 BM(4, 4, 0)
#define ARM64_MMFR0_ASIDBITS_MASK BM(4, 4, 15)

#define ARCH_DEFAULT_STACK_SIZE 8192

/* map 512GB at the base of the kernel. this is the max that can be mapped with a
 * single level 1 page table using 1GB pages.
 */
#ifndef __ASSEMBLER__
#define ARCH_PHYSMAP_SIZE (1UL << 39)
#else
#define ARCH_PHYSMAP_SIZE (0x1 << 39)
#endif
