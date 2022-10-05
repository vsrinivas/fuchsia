// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "legacy-mem-config.h"

#include <lib/zircon-internal/e820.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <efi/boot-services.h>

namespace {

constexpr size_t kEfiPageSize = 0x1000;

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
bool ParseEfiPayload(zbitl::ByteView payload, size_t* num_entries, size_t* entry_size) {
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

zbi_mem_range_t GetTableEntry(const internal::EfiTable& table, size_t n) {
  // Ensure the index is valid.
  ZX_ASSERT(n < table.num_entries);

  // Convert the n'th entry.
  size_t offset = sizeof(uint64_t) + n * table.entry_size;
  ZX_DEBUG_ASSERT(offset + sizeof(efi_memory_descriptor) <= table.payload.size());
  efi_memory_descriptor descriptor;
  memcpy(&descriptor, &table.payload[offset], sizeof(descriptor));
  return internal::ToMemRange(descriptor);
}

zbi_mem_range_t GetTableEntry(internal::E820Table table, size_t n) {
  return internal::ToMemRange(table[n]);
}

zbi_mem_range_t GetTableEntry(internal::MemConfigTable table, size_t n) { return table[n]; }

size_t GetTableSize(const internal::EfiTable& table) { return table.num_entries; }

size_t GetTableSize(internal::E820Table table) { return table.size(); }

size_t GetTableSize(internal::MemConfigTable table) { return table.size(); }

}  // namespace

namespace internal {
zbi_mem_range_t ToMemRange(const E820Entry& range) {
  return zbi_mem_range_t{
      .paddr = range.addr,
      .length = range.size,
      .type = range.type == E820Type::kRam ? static_cast<uint32_t>(ZBI_MEM_RANGE_RAM)
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
      .length = range.NumberOfPages * kEfiPageSize,
      .type = type,
      .reserved = 0,
  };
}

}  // namespace internal

fit::result<std::string_view, MemRangeTable> MemRangeTable::FromSpan(uint32_t zbi_type,
                                                                     zbitl::ByteView payload) {
  MemRangeTable result;
  switch (zbi_type) {
    case kLegacyZbiTypeE820Table:
      if (payload.size() % sizeof(E820Entry) != 0) {
        return fit::error("Invalid size for E820 table");
      };
      result.table_ = internal::E820Table{zbitl::AsSpan<const E820Entry>(payload)};
      break;

    case ZBI_TYPE_MEM_CONFIG:
      if (payload.size() % sizeof(zbi_mem_range_t) != 0) {
        return fit::error("Invalid size for MemConfig table");
      }
      result.table_ = internal::MemConfigTable{zbitl::AsSpan<const zbi_mem_range_t>(payload)};
      break;

    case kLegacyZbiTypeEfiMemoryMap: {
      size_t num_entries;
      size_t entry_size;
      if (!ParseEfiPayload(payload, &num_entries, &entry_size)) {
        return fit::error("Could not parse EFI memory map");
      }
      result.table_ = internal::EfiTable{
          .num_entries = num_entries,
          .entry_size = entry_size,
          .payload = payload,
      };
      break;
    }
    default:
      return fit::error("Unknown memory table type");
  }
  return fit::ok(result);
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
