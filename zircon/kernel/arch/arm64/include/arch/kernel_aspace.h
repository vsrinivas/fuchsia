// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_KERNEL_ASPACE_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_KERNEL_ASPACE_H_

// NOTE: This is an independent header so it can be #include'd by a userland
// test program as well as the kernel source (both C++ and assembly).

// Virtual address where the kernel address space begins.
// Below this is the user address space.
#define KERNEL_ASPACE_BASE 0xffff000000000000UL
#define KERNEL_ASPACE_SIZE 0x0001000000000000UL

// Virtual address where the user-accessible address space begins.
// Below this is wholly inaccessible.
#define USER_ASPACE_BASE 0x0000000001000000UL
#define USER_ASPACE_SIZE (0xffffff000000UL - USER_ASPACE_BASE)

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_KERNEL_ASPACE_H_
