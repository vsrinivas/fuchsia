// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/apic.h>
#include <lib/acpi_lite/structures.h>
#include <lib/zx/status.h>
#include <zircon/types.h>

#include <fbl/function.h>
#include <fbl/span.h>

#include "binary_reader.h"
#include "debug.h"

namespace acpi_lite {
namespace {

template <typename T, typename F>
zx_status_t ForEachMadtEntryOfType(const AcpiParserInterface& parser, uint8_t type, F visitor) {
  const AcpiMadtTable* table = GetTableByType<AcpiMadtTable>(parser);
  if (table == nullptr) {
    LOG_INFO("SystemCpuCount: Could not find MADT table.\n");
    return ZX_ERR_NOT_FOUND;
  }

  BinaryReader reader = BinaryReader::FromPayloadOfStruct(table);
  while (!reader.empty()) {
    // Read the header.
    //
    // If we get a short read (i.e., the reader isn't empty, but we can't read a full header),
    // then the table is corrupt.
    const AcpiSubTableHeader* header = reader.Read<AcpiSubTableHeader>();
    if (header == nullptr) {
      LOG_INFO("SystemCpuCount: Malformed MADT table.\n");
      return ZX_ERR_INTERNAL;
    }

    // If the type is incorrect, keep searching.
    if (header->type != type) {
      continue;
    }

    // Ensure the length is valid for an object of type T.
    if (header->length < sizeof(T)) {
      return ZX_ERR_INTERNAL;
    }

    // Call the visitor.
    zx_status_t status = visitor(reinterpret_cast<const T&>(*header));
    if (status != ZX_OK) {
      return status;
    }
  }

  return ZX_OK;
}

}  // namespace

zx_status_t EnumerateProcessorLocalApics(
    const AcpiParserInterface& parser,
    const fbl::Function<zx_status_t(const AcpiMadtLocalApicEntry&)>& callback) {
  return ForEachMadtEntryOfType<AcpiMadtLocalApicEntry>(
      parser, ACPI_MADT_TYPE_LOCAL_APIC, [&callback](const AcpiMadtLocalApicEntry& record) {
        if (!(record.flags & ACPI_MADT_FLAG_ENABLED)) {
          return ZX_OK;
        }
        return callback(record);
      });
}

zx_status_t EnumerateIoApics(
    const AcpiParserInterface& parser,
    const fbl::Function<zx_status_t(const AcpiMadtIoApicEntry&)>& callback) {
  return ForEachMadtEntryOfType<AcpiMadtIoApicEntry, decltype(callback)>(
      parser, ACPI_MADT_TYPE_IO_APIC, callback);
}

zx_status_t EnumerateIoApicIsaOverrides(
    const AcpiParserInterface& parser,
    const fbl::Function<zx_status_t(const AcpiMadtIntSourceOverrideEntry&)>& callback) {
  return ForEachMadtEntryOfType<AcpiMadtIntSourceOverrideEntry, decltype(callback)>(
      parser, ACPI_MADT_TYPE_INT_SOURCE_OVERRIDE, callback);
}

}  // namespace acpi_lite
