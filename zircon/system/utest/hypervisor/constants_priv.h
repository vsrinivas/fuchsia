// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_HYPERVISOR_CONSTANTS_PRIV_H_
#define ZIRCON_SYSTEM_UTEST_HYPERVISOR_CONSTANTS_PRIV_H_

#include <limits.h>

// clang-format off

#define VMO_SIZE        0x1000000
#define TRAP_PORT       0x11
#define TRAP_ADDR       (VMO_SIZE - PAGE_SIZE * 2)
#define EXIT_TEST_ADDR  (VMO_SIZE - PAGE_SIZE)

#if __x86_64__
#define GUEST_ENTRY     0x2000
#define X86_CR0_ET      0x00000010 /* extension type */
#define X86_CR0_NE      0x00000020 /* enable x87 exception */
#define X86_CR0_NW      0x20000000 /* not write-through */
#define X86_CR0_CD      0x40000000 /* cache disable */
#define X86_CR4_OSFXSR  0x00000200 /* os supports fxsave */
#endif

#endif  // ZIRCON_SYSTEM_UTEST_HYPERVISOR_CONSTANTS_PRIV_H_
