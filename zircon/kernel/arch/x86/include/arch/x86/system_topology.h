// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_SYSTEM_TOPOLOGY_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_SYSTEM_TOPOLOGY_H_

#include <lib/acpi_tables.h>
#include <zircon/boot/image.h>

#include <arch/x86/cpuid.h>
#include <fbl/vector.h>

namespace x86 {
// Generates the system topology.
// Exposed for testing.
zx_status_t GenerateFlatTopology(const cpu_id::CpuId& cpuid, const AcpiTables& acpi_tables,
                                 fbl::Vector<zbi_topology_node_t>* topology);

}  // namespace x86

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_SYSTEM_TOPOLOGY_H_
