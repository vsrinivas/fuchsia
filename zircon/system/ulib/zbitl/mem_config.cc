// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mem_config.h"

#include <inttypes.h>
#include <lib/stdcompat/span.h>
#include <lib/zbitl/items/mem_config.h>
#include <lib/zbitl/view.h>
#include <lib/zx/status.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/boot/e820.h>
#include <zircon/boot/image.h>
#include <zircon/limits.h>

#include <efi/boot-services.h>

namespace zbitl {

using zbitl::internal::E820Table;
using zbitl::internal::EfiTable;
using zbitl::internal::MemConfigTable;
using zbitl::internal::ToMemRange;

namespace {

// Ensure the given payload is a valid EFI memory table.
//
// The EFI memory dump format is described in the UEFI Spec (version 2.8),
// Section 7.2 under "EFI_BOOT_SERVICES.GetMemoryMap()".
//
// The format consists of a 64-bit `entry_size` value, followed by one or more
// table entries. Each table entry consists of `entry_size` bytes, the
// beginning of each containing a `efi_memory_descriptor` structure.
//
// We return "true" if the table is valid, along with the number of entries and
// the size of each entry. Otherwise, we return false.
bool ParseEfiPayload(ByteView payload, size_t* num_entries, size_t* entry_size) {
  *num_entries = 0;
  *entry_size = 0;
  if (payload.size() < sizeof(uint64_t)) {
    return false;
  }
  *entry_size = static_cast<size_t>(*reinterpret_cast<const uint64_t*>(payload.data()));
  if (*entry_size < sizeof(efi_memory_descriptor)) {
    return false;
  }
  if (*entry_size % alignof(efi_memory_descriptor) != 0) {
    return false;
  }
  if ((payload.size() - sizeof(uint64_t)) % *entry_size != 0) {
    return false;
  }

  *num_entries = (payload.size() - sizeof(uint64_t)) / *entry_size;
  return true;
}

zbi_mem_range_t GetTableEntry(const EfiTable& table, size_t n) {
  // Ensure the index is valid.
  ZX_ASSERT(n < table.num_entries);

  // Convert the n'th entry.
  size_t offset = sizeof(uint64_t) + n * table.entry_size;
  ZX_DEBUG_ASSERT(offset + sizeof(efi_memory_descriptor) <= table.payload.size());
  efi_memory_descriptor descriptor;
  memcpy(&descriptor, &table.payload[offset], sizeof(descriptor));
  return ToMemRange(descriptor);
}

zbi_mem_range_t GetTableEntry(const E820Table& table, size_t n) {
  return ToMemRange(table.table[n]);
}

zbi_mem_range_t GetTableEntry(const MemConfigTable& table, size_t n) { return table.table[n]; }

size_t GetTableSize(const EfiTable& table) { return table.num_entries; }

size_t GetTableSize(const E820Table& table) { return table.table.size(); }

size_t GetTableSize(const MemConfigTable& table) { return table.table.size(); }

// Return "true" if the given payload type is a memory range table type.
size_t IsMemRangeType(uint32_t type) {
  switch (type) {
    case ZBI_TYPE_E820_TABLE:
    case ZBI_TYPE_MEM_CONFIG:
    case ZBI_TYPE_EFI_MEMORY_MAP:
      return true;
    default:
      return false;
  }
}

}  // namespace

namespace internal {

zbi_mem_range_t ToMemRange(const e820entry_t& range) {
  return zbi_mem_range_t{
      .paddr = range.addr,
      .length = range.size,
      .type = range.type == E820_RAM ? static_cast<uint32_t>(ZBI_MEM_RANGE_RAM)
                                     : static_cast<uint32_t>(ZBI_MEM_RANGE_RESERVED),
      .reserved = 0,
  };
}

zbi_mem_range_t ToMemRange(const efi_memory_descriptor& range) {
  const uint32_t type = [&range]() {
    switch (range.Type) {
      case EfiLoaderCode:
      case EfiLoaderData:
      case EfiBootServicesCode:
      case EfiBootServicesData:
      case EfiConventionalMemory:
        return ZBI_MEM_RANGE_RAM;
      default:
        return ZBI_MEM_RANGE_RESERVED;
    }
  }();
  return zbi_mem_range_t{
      .paddr = range.PhysicalStart,
      .length = range.NumberOfPages * ZX_PAGE_SIZE,
      .type = type,
      .reserved = 0,
  };
}

}  // namespace internal

fitx::result<std::string_view, MemRangeTable> MemRangeTable::FromView(View<ByteView> view) {
  // Find the last memory table in the ZBI.
  View<ByteView>::iterator table = view.end();
  for (View<ByteView>::iterator it = view.begin(); it != view.end(); ++it) {
    if (IsMemRangeType(it->header->type)) {
      table = it;
      // keep searching, in case there is another item later.
    }
  }

  // Return any errors we encountered during iteration.
  if (auto error = view.take_error(); error.is_error()) {
    return fitx::error(error.error_value().zbi_error);
  }

  // If nothing was found, return an error.
  if (table == view.end()) {
    return fitx::error("No memory information found.");
  }

  // Return the table.
  return FromItem(table);
}

fitx::result<std::string_view, MemRangeTable> MemRangeTable::FromItem(View<ByteView>::iterator it) {
  return FromSpan(it->header->type, it->payload);
}

fitx::result<std::string_view, MemRangeTable> MemRangeTable::FromSpan(uint32_t zbi_type,
                                                                      ByteView payload) {
  MemRangeTable result;
  switch (zbi_type) {
    case ZBI_TYPE_E820_TABLE:
      if (payload.size() % sizeof(e820entry_t) != 0) {
        return fitx::error("Invalid size for E820 table");
      };
      result.table_ = E820Table{AsSpan<const e820entry_t>(payload)};
      break;

    case ZBI_TYPE_MEM_CONFIG:
      if (payload.size() % sizeof(zbi_mem_range_t) != 0) {
        return fitx::error("Invalid size for MemConfig table");
      }
      result.table_ = internal::MemConfigTable{AsSpan<const zbi_mem_range_t>(payload)};
      break;

    case ZBI_TYPE_EFI_MEMORY_MAP: {
      size_t num_entries;
      size_t entry_size;
      if (!ParseEfiPayload(payload, &num_entries, &entry_size)) {
        return fitx::error("Could not parse EFI memory map");
      }
      result.table_ = EfiTable{
          .num_entries = num_entries,
          .entry_size = entry_size,
          .payload = payload,
      };
      break;
    }
    default:
      return fitx::error("Unknown memory table type");
  }
  return fitx::ok(result);
}

zbi_mem_range_t MemRangeTable::operator[](size_t n) const {
  return std::visit([n](const auto& table) -> zbi_mem_range_t { return GetTableEntry(table, n); },
                    table_);
}

size_t MemRangeTable::size() const {
  return std::visit([](const auto& table) -> size_t { return GetTableSize(table); }, table_);
}

bool MemRangeTable::iterator::operator==(const iterator& other) const {
  return std::tie(parent_, offset_) == std::tie(other.parent_, other.offset_);
}

zbi_mem_range_t MemRangeTable::iterator::operator*() const {
  ZX_DEBUG_ASSERT_MSG(parent_ != nullptr, "Attempting to access invalid iterator.");
  return (*parent_)[offset_];
}

// Convert a zbi_mem_range_t memory type into a human-readable string.
std::string_view MemRangeTypeName(uint32_t type) {
  switch (type) {
    case ZBI_MEM_RANGE_RAM:
      return "RAM";
    case ZBI_MEM_RANGE_PERIPHERAL:
      return "peripheral";
    case ZBI_MEM_RANGE_RESERVED:
      return "reserved";
    default:
      return {};
  }
}

}  // namespace zbitl
