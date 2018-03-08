// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ADDRESS_H_
#define GARNET_LIB_MACHINA_ADDRESS_H_

#include <limits.h>

namespace machina {

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

// GIC v2 distributor memory range.
static constexpr uint64_t kGicv2DistributorPhysBase         = 0xe82b1000;
static constexpr uint64_t kGicv2DistributorSize             = PAGE_SIZE;

// GIC v3 distributor memory range.
static constexpr uint64_t kGicv3DistributorPhysBase         = 0xe82b0000;
static constexpr uint64_t kGicv3DistributorSize             = 0x10000;

// For GICv3, currently assuming we are running only 1 CPU, in future when we have multiple
// core support we need to add the other Redistributor regions as well
// GIC v3 Redistributor memory range.
static constexpr uint64_t kGicv3ReDistributorPhysBase       = 0xe8350000; // GICR_RD_BASE
static constexpr uint64_t kGicv3ReDistributorSize           = 0x10000;

// GIC v3 Redistributor memory range.
static constexpr uint64_t kGicv3ReDistributor_SGIPhysBase   = 0xe8360000; // GICR_SGI_BASE
static constexpr uint64_t kGicv3ReDistributor_SGISize       = 0x10000;

// IO APIC memory range.
static constexpr uint64_t kIoApicPhysBase                   = 0xfec00000;
static constexpr uint64_t kIoApicSize                       = PAGE_SIZE;

// PCI memory ranges.
#if __aarch64__
static constexpr uint64_t kPciMmioBarPhysBase               = 0x10000000;
static constexpr uint64_t kPciEcamPhysBase                  = 0x3f000000;
#elif __x86_64__
static constexpr uint64_t kPciMmioBarPhysBase               = 0xf0000000;
static constexpr uint64_t kPciEcamPhysBase                  = 0xd0000000;
#endif
static constexpr uint64_t kPciEcamPhysTop                   = kPciEcamPhysBase + pci_ecam_size(0, 1) - 1;

// TPM memory range.
static constexpr uint64_t kTpmPhysBase                      = 0xfed40000;
static constexpr uint64_t kTpmSize                          = 0x5000;

// PL011 memory range.
static constexpr uint64_t kPl011PhysBase                    = 0xfff32000;
static constexpr uint64_t kPl011Size                        = PAGE_SIZE;

// PL031 memory range.
static constexpr uint64_t kPl031PhysBase                    = 0x09010000;
static constexpr uint64_t kPl031Size                        = PAGE_SIZE;

// I8250 ports.
static constexpr uint64_t kI8250Base0                       = 0x3f8;
static constexpr uint64_t kI8250Base1                       = 0x2f8;
static constexpr uint64_t kI8250Base2                       = 0x3e8;
static constexpr uint64_t kI8250Base3                       = 0x2e8;
static constexpr uint64_t kI8250Size                        = 0x8;

// CMOS ports.
static constexpr uint64_t kCmosBase                         = 0x70;
static constexpr uint64_t kCmosSize                         = 0x2;

// I8042 ports.
static constexpr uint64_t kI8042Base                        = 0x60;

// Power states as defined in the DSDT.
//
// We only implement a transition from S0 to S5 to trigger guest termination.
static constexpr uint64_t kSlpTyp5                          = 0x1;

// PIC ports.
static constexpr uint64_t kPic1Base                         = 0x20;
static constexpr uint64_t kPic2Base                         = 0xa0;
static constexpr uint64_t kPicSize                          = 0x2;

// PIT ports.
static constexpr uint64_t kPitBase                          = 0x40;
static constexpr uint64_t kPitSize                          = 0x4;
static constexpr uint64_t kPitChannel0                      = 0x40;
static constexpr uint64_t kPitControlPort                   = 0x43;

// PM1 ports.
static constexpr uint64_t kPm1EventPort                     = 0x1000;
static constexpr uint64_t kPm1ControlPort                   = 0x2000;

// PCI config ports.
static constexpr uint64_t kPciConfigPortBase                = 0xcf8;
static constexpr uint64_t kPciConfigPortSize                = 0x8;

// clang-format on

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ADDRESS_H_
