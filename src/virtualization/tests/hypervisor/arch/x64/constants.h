// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_HYPERVISOR_ARCH_X64_CONSTANTS_H_
#define SRC_VIRTUALIZATION_TESTS_HYPERVISOR_ARCH_X64_CONSTANTS_H_

#define X86_CR0_ET 0x00000010      // extension type
#define X86_CR0_NE 0x00000020      // enable x87 exception
#define X86_CR0_NW 0x20000000      // not write-through
#define X86_CR0_CD 0x40000000      // cache disable
#define X86_CR4_OSFXSR 0x00000200  // os supports fxsave

// Entry point for the guest.
#define GUEST_ENTRY 0x2000

#endif  // SRC_VIRTUALIZATION_TESTS_HYPERVISOR_ARCH_X64_CONSTANTS_H_
