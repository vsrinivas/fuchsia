// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_PAGE_TABLES_INCLUDE_ARCH_X86_PAGE_TABLES_CONSTANTS_H_
#define ZIRCON_KERNEL_ARCH_X86_PAGE_TABLES_INCLUDE_ARCH_X86_PAGE_TABLES_CONSTANTS_H_

#include <arch/kernel_aspace.h>
#include <page_tables/x86/constants.h>

/* on x86-64 physical memory is mapped at the base of the kernel address space */
#define X86_PHYS_TO_VIRT(x) ((uintptr_t)(x) + KERNEL_ASPACE_BASE)
#define X86_VIRT_TO_PHYS(x) ((uintptr_t)(x)-KERNEL_ASPACE_BASE)

#endif  // ZIRCON_KERNEL_ARCH_X86_PAGE_TABLES_INCLUDE_ARCH_X86_PAGE_TABLES_CONSTANTS_H_
