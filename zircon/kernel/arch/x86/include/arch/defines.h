// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_DEFINES_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_DEFINES_H_

#define PAGE_SIZE 4096
#define PAGE_SIZE_SHIFT 12
#define PAGE_MASK (PAGE_SIZE - 1)

#define MAX_CACHE_LINE 64

#define ARCH_DEFAULT_STACK_SIZE 8192

#define ARCH_PHYSMAP_SIZE (0x1000000000UL)  // 64GB

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_DEFINES_H_
