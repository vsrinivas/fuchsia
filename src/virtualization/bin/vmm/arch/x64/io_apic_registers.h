// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_REGISTERS_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_REGISTERS_H_

#include "src/virtualization/bin/vmm/arch/x64/io_apic.h"

// clang-format off

// IO APIC register addresses.
#define IO_APIC_IOREGSEL                0x00
#define IO_APIC_IOWIN                   0x10
#define IO_APIC_EOIR                    0x40

// IO APIC indirect register addresses.
#define IO_APIC_REGISTER_ID             0x00
#define IO_APIC_REGISTER_VER            0x01
#define IO_APIC_REGISTER_ARBITRATION    0x02

// IO APIC configuration constants.
#define IO_APIC_VERSION                 0x20
#define FIRST_REDIRECT_OFFSET           0x10
#define LAST_REDIRECT_OFFSET            (FIRST_REDIRECT_OFFSET + IoApic::kNumRedirectOffsets - 1)

// DESTMOD register.
#define IO_APIC_DESTMOD_PHYSICAL        0x00
#define IO_APIC_DESTMOD_LOGICAL         0x01

// clang-format on

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_REGISTERS_H_
