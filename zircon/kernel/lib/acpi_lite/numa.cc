// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/numa.h>
#include <lib/acpi_lite/structures.h>

#include "binary_reader.h"
#include "debug.h"

namespace acpi_lite {

zx_status_t EnumerateCpuNumaPairs(
    const AcpiSratTable* const srat,
    const fbl::Function<void(const AcpiNumaDomain&, uint32_t)>& callback) {
  // Initialise the domains.
  static constexpr size_t kMaxNumaDomains = 10;
  AcpiNumaDomain domains[kMaxNumaDomains];
  for (uint32_t i = 0; i < kMaxNumaDomains; i++) {
    domains[i] = AcpiNumaDomain{
        .domain = i,
        .memory = {},
        .memory_count = 0,
    };
  }

  // First find all NUMA domains.
  BinaryReader reader = BinaryReader::FromPayloadOfStruct(srat);
  while (!reader.empty()) {
    const AcpiSubTableHeader* sub_header = reader.Read<AcpiSubTableHeader>();
    if (sub_header == nullptr) {
      return ZX_ERR_INTERNAL;
    }

    // Ignore anything not ACPI_SRAT_TYPE_MEMORY_AFFINITY.
    if (sub_header->type != ACPI_SRAT_TYPE_MEMORY_AFFINITY) {
      continue;
    }

    const auto* mem = Downcast<AcpiSratMemoryAffinityEntry>(sub_header);
    if (mem == nullptr) {
      return ZX_ERR_INTERNAL;
    }

    // Ignore disabled entries.
    if (!(mem->flags & ACPI_SRAT_FLAG_ENABLED)) {
      continue;
    }

    // Ensure proximity domain is valid.
    if (mem->proximity_domain >= kMaxNumaDomains) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    // Ensure we haven't seen too many entries for this domain.
    auto& domain = domains[mem->proximity_domain];
    if (domain.memory_count >= kAcpiMaxNumaRegions) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    uint64_t base = (static_cast<uint64_t>(mem->base_address_high) << 32) | mem->base_address_low;
    uint64_t length = (static_cast<uint64_t>(mem->length_high) << 32) | mem->length_low;
    domain.memory[domain.memory_count++] = {.base_address = base, .length = length};

    LOG_DEBUG("acpi_lite: ACPI SRAT: numa Region:{ domain: %u base: %#lx length: %#lx (%lu) }\n",
              mem->proximity_domain, base, length, length);
  }

  // Then visit all CPU APIC IDs and provide the accompanying NUMA region.
  reader = BinaryReader::FromPayloadOfStruct(srat);
  while (!reader.empty()) {
    const AcpiSubTableHeader* sub_header = reader.Read<AcpiSubTableHeader>();
    if (sub_header == nullptr) {
      return ZX_ERR_INTERNAL;
    }

    if (sub_header->type == ACPI_SRAT_TYPE_PROCESSOR_AFFINITY) {
      const auto* cpu = Downcast<AcpiSratProcessorAffinityEntry>(sub_header);
      if (cpu == nullptr) {
        return ZX_ERR_INTERNAL;
      }

      // Ignore disabled entries.
      if (!(cpu->flags & ACPI_SRAT_FLAG_ENABLED)) {
        continue;
      }

      // Ensure the domain is in bounds.
      uint32_t domain = cpu->proximity_domain();
      if (domain >= kMaxNumaDomains) {
        return ZX_ERR_INTERNAL;
      }

      callback(domains[domain], cpu->apic_id);
    } else if (sub_header->type == ACPI_SRAT_TYPE_PROCESSOR_X2APIC_AFFINITY) {
      const auto* cpu = Downcast<AcpiSratProcessorX2ApicAffinityEntry>(sub_header);
      if (cpu == nullptr) {
        return ZX_ERR_INTERNAL;
      }

      // Ignore disabled entries.
      if (!(cpu->flags & ACPI_SRAT_FLAG_ENABLED)) {
        continue;
      }

      // Ensure it is in bounds.
      uint32_t domain = cpu->proximity_domain;
      if (domain >= kMaxNumaDomains) {
        return ZX_ERR_INTERNAL;
      }

      callback(domains[cpu->proximity_domain], cpu->x2apic_id);
    }
  }

  return ZX_OK;
}

zx_status_t EnumerateCpuNumaPairs(const AcpiParserInterface& parser,
                                  fbl::Function<void(const AcpiNumaDomain&, uint32_t)> callback) {
  // Get the SRAT table.
  const AcpiSratTable* srat = GetTableByType<AcpiSratTable>(parser);
  if (srat == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }

  return EnumerateCpuNumaPairs(srat, callback);
}

}  // namespace acpi_lite
