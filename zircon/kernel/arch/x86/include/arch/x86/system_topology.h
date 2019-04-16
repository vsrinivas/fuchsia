// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ARCH_X86_SYSTEM_TOPOLOGY_H
#define ARCH_X86_SYSTEM_TOPOLOGY_H

#include <arch/x86/cpuid.h>
#include <fbl/vector.h>
#include <lib/acpi_tables.h>
#include <zircon/boot/image.h>

namespace x86 {
// Generates the system topology.
// Exposed for testing.
zx_status_t GenerateFlatTopology(const cpu_id::CpuId& cpuid, const AcpiTables& acpi_tables,
                                 fbl::Vector<zbi_topology_node_t>* topology);

} // namespace x86

#endif // ARCH_X86_SYSTEM_TOPOLOGY_H
