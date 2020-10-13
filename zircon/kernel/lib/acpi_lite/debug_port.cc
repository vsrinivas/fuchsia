// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/debug_port.h>
#include <lib/acpi_lite/structures.h>
#include <lib/zx/status.h>
#include <zircon/types.h>

#include "binary_reader.h"
#include "debug.h"

namespace acpi_lite {

zx::status<AcpiDebugPortDescriptor> ParseAcpiDbg2Table(const AcpiDbg2Table& debug_table) {
  // Ensure there is at least one debug port.
  if (debug_table.num_entries < 1) {
    LOG_INFO("acpi_lite: DBG2 table contains no debug ports.\n");
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  // Read the first device by seeking to |offset| and reading an AcpiDbg2Device.
  BinaryReader reader = BinaryReader::FromVariableSizedStruct(&debug_table);
  if (!reader.SkipBytes(debug_table.offset)) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  const AcpiDbg2Device* device = reader.Read<AcpiDbg2Device>();
  if (device == nullptr) {
    return zx::error(ZX_ERR_INTERNAL);
  }

  // Ensure we are a supported type.
  if (device->port_type != ACPI_DBG2_TYPE_SERIAL_PORT ||
      device->port_subtype != ACPI_DBG2_SUBTYPE_16550_COMPATIBLE) {
    LOG_INFO("acpi_lite: DBG2 debug port unsuported. (type=%x, subtype=%x)\n", device->port_type,
             device->port_subtype);
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // We need at least one register.
  if (device->register_count < 1) {
    LOG_INFO("acpi_lite: DBG2 debug port doesn't have any registers defined.\n");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Get base address.
  reader = BinaryReader::FromVariableSizedStruct(device);
  if (!reader.SkipBytes(device->base_address_offset)) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  const AcpiGenericAddress* address = reader.ReadFixedLength<AcpiGenericAddress>();
  if (address == nullptr) {
    return zx::error(ZX_ERR_INTERNAL);
  }

  // Get length.
  reader = BinaryReader::FromVariableSizedStruct(device);
  if (!reader.SkipBytes(device->address_size_offset)) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  const auto* length = reader.ReadFixedLength<Packed<uint32_t>>();
  if (length == nullptr) {
    return zx::error(ZX_ERR_INTERNAL);
  }

  // Ensure we read an MMIO address.
  if (address->address_space_id != ACPI_ADDR_SPACE_MEMORY) {
    LOG_INFO("acpi_lite: Address space unsupported (space_id=%x)\n", address->address_space_id);
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::success(AcpiDebugPortDescriptor{
      .address = static_cast<zx_paddr_t>(address->address),
      .length = length->value,
  });
}

zx::status<AcpiDebugPortDescriptor> GetDebugPort(const AcpiParserInterface& parser) {
  // Find the DBG2 table entry.
  const AcpiDbg2Table* debug_table = GetTableByType<AcpiDbg2Table>(parser);
  if (debug_table == nullptr) {
    LOG_INFO("acpi_lite: could not find debug port (v2) ACPI entry\n");
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  return ParseAcpiDbg2Table(*debug_table);
}

}  // namespace acpi_lite
