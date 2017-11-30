// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ADDRESS_H_
#define GARNET_LIB_MACHINA_ADDRESS_H_

#include <limits.h>

// The size of an ECAM region depends on values in the MCFG ACPI table. For
// each ECAM region there is a defined physical base address as well as a bus
// start/end value for that region.
//
// When creating an ECAM address for a PCI configuration register, the bus
// value must be relative to the starting bus number for that ECAM region.
static inline constexpr uint64_t pci_ecam_size(uint64_t start_bus,
                                               uint64_t end_bus) {
  return (end_bus - start_bus) << 20;
}

// clang-format off

// GIC distributor memory range.
static const uint64_t kGicDistributorPhysBase   = 0xe82b1000;
static const uint64_t kGicDistributorSize       = PAGE_SIZE;

// IO APIC memory range.
static const uint64_t kIoApicPhysBase           = 0xfec00000;
static const uint64_t kIoApicSize               = PAGE_SIZE;

// PCI memory ranges.
#if __aarch64__
static const uint64_t kPciMmioBarPhysBase       = 0x10000000;
static const uint64_t kPciEcamPhysBase          = 0x3f000000;
#elif __x86_64__
static const uint64_t kPciMmioBarPhysBase       = 0xf0000000;
static const uint64_t kPciEcamPhysBase          = 0xd0000000;
#endif
static const uint64_t kPciEcamPhysTop           = kPciEcamPhysBase +
                                                  pci_ecam_size(0, 1) - 1;

// TPM memory range.
static const uint64_t kTpmPhysBase              = 0xfed40000;
static const uint64_t kTpmSize                  = 0x5000;

// PL011 memory range.
static const uint64_t kPl011PhysBase            = 0xfff32000;
static const uint64_t kPl011Size                = PAGE_SIZE;

// PL031 memory range.
static const uint64_t kPl031PhysBase            = 0x09010000;
static const uint64_t kPl031Size                = PAGE_SIZE;

// I8250 ports.
static const uint64_t kI8250Base0               = 0x3f8;
static const uint64_t kI8250Base1               = 0x2f8;
static const uint64_t kI8250Base2               = 0x3e8;
static const uint64_t kI8250Base3               = 0x2e8;
static const uint64_t kI8250Size                = 0x8;

// RTC ports.
static const uint64_t kRtcBase                  = 0x70;
static const uint64_t kRtcSize                  = 0x2;

// I8042 ports.
static const uint64_t kI8042Base                = 0x60;

// Power states as defined in the DSDT.
//
// We only implement a transition from S0 to S5 to trigger guest termination.
static const uint64_t kSlpTyp5                  = 0x1;

// PIC ports.
static const uint64_t kPic1Base                 = 0x20;
static const uint64_t kPic2Base                 = 0xa0;
static const uint64_t kPicSize                  = 0x2;

// PIT ports.
static const uint64_t kPitBase                  = 0x40;
static const uint64_t kPitSize                  = 0x4;
static const uint64_t kPitChannel0              = 0x40;
static const uint64_t kPitControlPort           = 0x43;

// PCI config ports.
static const uint64_t kPciConfigPortBase        = 0xcf8;
static const uint64_t kPciConfigPortSize        = 0x8;

// clang-format on

#endif  // GARNET_LIB_MACHINA_ADDRESS_H_
