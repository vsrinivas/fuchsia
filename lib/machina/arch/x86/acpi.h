// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_X86_ACPI_H_
#define GARNET_LIB_MACHINA_ARCH_X86_ACPI_H_

#include <zircon/types.h>

namespace machina {

struct AcpiConfig {
  const char* dsdt_path;
  const char* mcfg_path;
  zx_vaddr_t io_apic_addr;
  size_t num_cpus;
};

zx_status_t create_acpi_table(const struct AcpiConfig& cfg,
                              uintptr_t addr,
                              size_t size,
                              uintptr_t acpi_off);

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ARCH_X86_ACPI_H_
