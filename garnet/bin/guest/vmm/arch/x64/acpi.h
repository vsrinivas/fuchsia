// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_ARCH_X64_ACPI_H_
#define GARNET_BIN_GUEST_VMM_ARCH_X64_ACPI_H_

#include "garnet/bin/guest/vmm/device/phys_mem.h"

// The address of the ACPI table is significant, as this is typically where the
// ACPICA library starts to scan for an ACPI RSDP. If we are unable to pass the
// address directly to a kernel, or if the address we pass is ignored, this
// provides a fallback method for locating it.
static constexpr uintptr_t kAcpiOffset = 0xe0000;

struct AcpiConfig {
  const char* dsdt_path;
  const char* mcfg_path;
  zx_vaddr_t io_apic_addr;
  size_t cpus;
};

zx_status_t create_acpi_table(const struct AcpiConfig& cfg,
                              const PhysMem& phys_mem);

#endif  // GARNET_BIN_GUEST_VMM_ARCH_X64_ACPI_H_
