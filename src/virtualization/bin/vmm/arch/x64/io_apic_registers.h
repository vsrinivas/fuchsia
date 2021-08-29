// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_REGISTERS_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_REGISTERS_H_

#include "src/virtualization/bin/vmm/arch/x64/io_apic.h"

// IO APIC register offsets.
enum IoApicOffset : uint8_t {
  kIoApicIoRegSel = 0x00,  // I/O register select.
  kIoApicIoWin = 0x10,     // I/O window.
  kIoApicEOIR = 0x40,      // End of interrupt register.
};

// IO APIC indirect register addresses.
enum IoApicRegister : uint8_t {
  kIoApicRegisterId = 0x00,
  kIoApicRegisterVer = 0x01,
  kIoApicRegisterArbitration = 0x02,
};

// IO APIC configuration constants.
constexpr uint8_t kIoApicVersion = 0x20;
constexpr uint8_t kFirstRedirectOffset = 0x10;
constexpr uint8_t kLastRedirectOffset = kFirstRedirectOffset + IoApic::kNumRedirectOffsets - 1;

// DESTMOD register.
constexpr uint8_t kIoApicDestmodPhysical = 0x00;
constexpr uint8_t kIoApicDestmodLogical = 0x01;

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_REGISTERS_H_
