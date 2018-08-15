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

#if __aarch64__

// For arm64, memory addresses must be in a 36-bit range. This is due to limits
// placed within the MMU code based on the limits of a Cortex-A53.
//
//See ARM DDI 0487B.b, Table D4-25 for the maximum IPA range that can be used.

// GIC v2 distributor memory range.
static constexpr uint64_t kGicv2DistributorPhysBase         = 0x800001000;
static constexpr uint64_t kGicv2DistributorSize             = 0x1000;

// GIC v3 distributor memory range.
static constexpr uint64_t kGicv3DistributorPhysBase         = 0x800000000;
static constexpr uint64_t kGicv3DistributorSize             = 0x10000;

// GIC v3 Redistributor memory range.
//
// See GIC v3.0/v4.0 Architecture Spec 8.10.
static constexpr uint64_t kGicv3RedistributorPhysBase       = 0x800010000; // GICR_RD_BASE
static constexpr uint64_t kGicv3RedistributorSize           = 0x10000;
static constexpr uint64_t kGicv3RedistributorSgiPhysBase    = 0x800020000; // GICR_SGI_BASE
static constexpr uint64_t kGicv3RedistributorSgiSize        = 0x10000;
static constexpr uint64_t kGicv3RedistributorStride         = 0x20000;
static_assert(kGicv3RedistributorPhysBase + kGicv3RedistributorSize == kGicv3RedistributorSgiPhysBase,
              "GICv3 Redistributor base and SGI base must be continguous");
static_assert(kGicv3RedistributorStride >= kGicv3RedistributorSize + kGicv3RedistributorSgiSize,
              "GICv3 Redistributor stride must be >= the size of a single mapping");

// PCI memory ranges.
static constexpr uint64_t kPciEcamPhysBase                  = 0x808100000;
static constexpr uint64_t kPciEcamSize                      = pci_ecam_size(0, 1);
static constexpr uint64_t kPciMmioBarPhysBase               = 0x808200000;
static constexpr uint64_t kPciMmioBarSize                   = 0x100000;

// PL011 memory range.
static constexpr uint64_t kPl011PhysBase                    = 0x808300000;
static constexpr uint64_t kPl011Size                        = 0x1000;

// PL031 memory range.
static constexpr uint64_t kPl031PhysBase                    = 0x808301000;
static constexpr uint64_t kPl031Size                        = 0x1000;

#elif __x86_64__

// For x86-64, memory addresses must be in a 32-bit range. For the IO APIC, this
// is due to the ACPICA library used by Zircon and Linux limiting the usable
// address to 32-bits. For PCI, both Zircon and Linux place further limits on
// where the PCI address region may be located.

// IO APIC memory range.
static constexpr uint64_t kIoApicPhysBase                   = 0xf8000000;
static constexpr uint64_t kIoApicSize                       = 0x1000;

// PCI memory ranges.
static constexpr uint64_t kPciEcamPhysBase                  = 0xf8100000;
static constexpr uint64_t kPciEcamSize                      = pci_ecam_size(0, 1);
static constexpr uint64_t kPciMmioBarPhysBase               = 0xf8200000;
static constexpr uint64_t kPciMmioBarSize                   = 0x100000;

// For x86-64, port IO addresses must be in a 16-bit range.

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

// PM1 ports.
static constexpr uint64_t kPm1EventPort                     = 0x1000;
static constexpr uint64_t kPm1ControlPort                   = 0x2000;

// PCI config ports.
static constexpr uint64_t kPciConfigPortBase                = 0xcf8;
static constexpr uint64_t kPciConfigPortSize                = 0x8;

#endif

// clang-format on

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ADDRESS_H_
