// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_KERNEL_ASPACE_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_KERNEL_ASPACE_H_

// NOTE: This is an independent header so it can be #include'd
// by a userland test program as well as the kernel source.

// Virtual address where the kernel address space begins.
// Below this is the user address space.
#define KERNEL_ASPACE_BASE 0xffffff8000000000UL  // -512GB
#define KERNEL_ASPACE_SIZE 0x0000008000000000UL

// Virtual address where the user-accessible address space begins.
// Below this is wholly inaccessible.
#define USER_ASPACE_BASE 0x0000000001000000UL  // 16MB

// We set the top of user address space to be (1 << 47) - 4k.
// See //docs/concepts/kernel/sysret_problem.md for why we subtract 4k here.
#define USER_ASPACE_SIZE ((1ULL << 47) - 4096 - USER_ASPACE_BASE)

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_KERNEL_ASPACE_H_
