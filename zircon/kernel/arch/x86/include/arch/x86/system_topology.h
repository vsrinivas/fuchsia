// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_SYSTEM_TOPOLOGY_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_SYSTEM_TOPOLOGY_H_

#include <lib/acpi_lite.h>
#include <lib/arch/x86/apic-id.h>
#include <lib/arch/x86/cache.h>
#include <zircon/boot/image.h>

#include <fbl/vector.h>
#include <ktl/forward.h>

namespace x86 {
namespace internal {

// Serves to hide the implementation of the function below.
zx_status_t GenerateFlatTopology(const arch::ApicIdDecoder& decoder,  //
                                 uint32_t primary_apic_id,            //
                                 const arch::CpuCacheInfo& cache_info,
                                 const acpi_lite::AcpiParserInterface& parser,
                                 fbl::Vector<zbi_topology_node_t>* topology);
}  // namespace internal

// Generates the system topology.
// Exposed for testing.
template <typename CpuidIoProvider>
zx_status_t GenerateFlatTopology(CpuidIoProvider&& io, const acpi_lite::AcpiParserInterface& parser,
                                 fbl::Vector<zbi_topology_node_t>* topology) {
  const uint32_t primary_apic_id = arch::GetApicId(io);
  const arch::ApicIdDecoder decoder(io);
  const arch::CpuCacheInfo cache_info(ktl::forward<CpuidIoProvider>(io));
  return internal::GenerateFlatTopology(decoder, primary_apic_id, cache_info, parser, topology);
}

}  // namespace x86

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_SYSTEM_TOPOLOGY_H_
