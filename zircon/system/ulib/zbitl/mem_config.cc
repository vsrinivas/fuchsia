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
#include <fbl/span.h>

namespace zbitl {
namespace {

using zbitl::internal::ToMemRange;

// Reinterpret the given ByteView as span of type T.
//
// If the sizeof(T) doesn't evenly divide the size of the byte view,
// a span covering all complete instances of T will be returned.
template <typename T>
inline cpp20::span<const T> AsSpan(ByteView bytes) {
  return {reinterpret_cast<const T*>(bytes.data()), bytes.size() / sizeof(T)};
}

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

// Get the number of entries in the given EFI memory table.
size_t GetEfiEntryCount(ByteView payload) {
  size_t num_entries, entry_size;
  if (!ParseEfiPayload(payload, &num_entries, &entry_size)) {
    return 0;
  }
  return num_entries;
}

// Get the n'th entry in the given EFI memory table, or nullptr if no such entry
// exists.
const efi_memory_descriptor* GetEfiEntry(ByteView payload, size_t n) {
  // Parse the table structure.
  size_t num_entries, entry_size;
  if (!ParseEfiPayload(payload, &num_entries, &entry_size)) {
    return nullptr;
  }

  // Ensure the index is valid.
  if (n >= num_entries) {
    return nullptr;
  }

  // Get the n'th entry.
  size_t offset = sizeof(uint64_t) + n * entry_size;
  ZX_DEBUG_ASSERT(offset + sizeof(efi_memory_descriptor) <= payload.size());
  return reinterpret_cast<const efi_memory_descriptor*>(&payload[offset]);
}

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
// Perform basic validation on the given ZBI payload, assuming it
// is encoded as the given type.
//
// Return fitx::ok() if the payload was valid, otherwise a std::string_view
// describing the error.
fitx::result<std::string_view> ValidateMemRangePayload(uint32_t type, ByteView payload) {
  switch (type) {
    case ZBI_TYPE_E820_TABLE:
      if (payload.size() % sizeof(e820entry_t) != 0) {
        return fitx::error("Invalid size for E820 table");
      };
      break;
    case ZBI_TYPE_MEM_CONFIG:
      if (payload.size() % sizeof(zbi_mem_range_t) != 0) {
        return fitx::error("Invalid size for MemConfig table");
      }
      break;
    case ZBI_TYPE_EFI_MEMORY_MAP: {
      size_t num_entries;
      size_t entry_size;
      if (!ParseEfiPayload(payload, &num_entries, &entry_size)) {
        return fitx::error("Could not parse EFI memory map");
      }
      break;
    }
    default:
      return fitx::error("Unknown memory table type");
  }

  return fitx::ok();
}

// Get the number of memory ranges in the given ZBI payload, assuming it
// is encoded as the given type.
size_t MemRangeElementCount(uint32_t type, ByteView payload) {
  switch (type) {
    case ZBI_TYPE_E820_TABLE:
      return payload.size() / sizeof(e820entry_t);
    case ZBI_TYPE_MEM_CONFIG:
      return payload.size() / sizeof(zbi_mem_range_t);
    case ZBI_TYPE_EFI_MEMORY_MAP:
      return GetEfiEntryCount(payload);
    default:
      return 0;
  }
}

// Get the n'th element from the given ZBI payload.
zbi_mem_range_t MemRangeElement(uint32_t type, ByteView payload, size_t n) {
  switch (type) {
    case ZBI_TYPE_E820_TABLE:
      return ToMemRange(AsSpan<e820entry_t>(payload)[n]);
    case ZBI_TYPE_MEM_CONFIG:
      return AsSpan<zbi_mem_range_t>(payload)[n];
    case ZBI_TYPE_EFI_MEMORY_MAP: {
      const efi_memory_descriptor* descriptor = GetEfiEntry(payload, n);
      // Calling code should ensure that it only requests valid entries.
      ZX_DEBUG_ASSERT(descriptor != nullptr);
      return ToMemRange(*descriptor);
    }
    default:
      ZX_PANIC("Unknown payload type %#" PRIx32 "\n", type);
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

MemRangeTable::MemRangeTable() = default;

MemRangeTable::MemRangeTable(View<ByteView> view) : view_(std::move(view)) {}

MemRangeTable::iterator::iterator(MemRangeTable* parent) : parent_(parent) {
  // Search for the first table element.
  if (parent_ != nullptr) {
    ++*this;
  }
}

MemRangeTable::iterator::iterator(MemRangeTable* parent, View<ByteView>::iterator it, size_t offset)
    : parent_(parent), it_(it), offset_(offset) {}

MemRangeTable::iterator MemRangeTable::end() {
  return MemRangeTable::iterator(this, view_.end(), 0);
}

MemRangeTable::iterator MemRangeTable::begin() {
  // Clear any pending error.
  error_ = fitx::ok();

  // If we have a default-constructed ZBI, just return an empty iterator.
  if (view_.storage().empty()) {
    return end();
  }

  // Otherwise, begin iteration.
  return MemRangeTable::iterator(this);
}

fitx::result<std::string_view, size_t> MemRangeTable::size() const {
  // We create a new view to avoid setting or clearing any pending error in `view_`.
  zbitl::View view(view_.storage());

  size_t count = 0;
  for (auto it = view.begin(); it != view.end(); ++it) {
    // Ignore items that are not memory ranges.
    if (!IsMemRangeType((*it).header->type)) {
      continue;
    }

    // Validate the memory range.
    if (fitx::result<std::string_view> result =
            ValidateMemRangePayload((*it).header->type, (*it).payload);
        result.is_error()) {
      view.ignore_error();  // ignore any iteration error
      return result.take_error();
    }

    // Count the number of items.
    count += MemRangeElementCount((*it).header->type, (*it).payload);
  }

  // Return any error encountered.
  if (auto status = view.take_error(); status.is_error()) {
    return fitx::error(status.error_value().zbi_error);
  }
  return fitx::ok(count);
}

bool MemRangeTable::iterator::operator==(const iterator& other) const {
  return std::tie(parent_, it_, offset_) == std::tie(other.parent_, other.it_, other.offset_);
}

zbi_mem_range_t MemRangeTable::iterator::operator*() const {
  // Ensure user has called `Next()` at least once, and hasn't reached the end.
  ZX_DEBUG_ASSERT_MSG(parent_ != nullptr, "Attempting to access invalid iterator.");
  ZX_DEBUG_ASSERT_MSG(it_.has_value() && *it_ != parent_->view_.end(),
                      "Attempting to access invalid iterator.");

  return MemRangeElement((**it_).header->type, (**it_).payload, offset_);
}

MemRangeTable::iterator& MemRangeTable::iterator::operator++() {
  // Ensure we are not already at end or default-constructed.
  ZX_DEBUG_ASSERT(parent_ != nullptr);

  // If we have already started looking at a particular ZBI item's
  // payload, keep searching through it until we reach the end.
  if (it_.has_value()) {
    size_t zbi_item_size = MemRangeElementCount((**it_).header->type, (**it_).payload);
    ++offset_;
    if (offset_ < zbi_item_size) {
      return *this;
    }
  }

  // Either the previous ZBI item's payload has been exhausted, or this
  // is our first call.
  //
  // Move to the next/first element.
  if (!it_.has_value()) {
    it_ = parent_->view_.begin();
  } else {
    ++(*it_);
  }

  // Keep searching until we find a valid payload.
  while (it_ != parent_->view_.end()) {
    uint32_t type = (**it_).header->type;

    // Ignore items that are non memory range tables.
    if (!IsMemRangeType(type)) {
      ++(*it_);
      continue;
    }

    // If the item is invalid, record an error and stop iterating.
    if (fitx::result<std::string_view> result = ValidateMemRangePayload(type, (**it_).payload);
        result.is_error()) {
      parent_->error_ = result;
      break;
    }

    // Skip over empty payloads.
    size_t zbi_item_size = MemRangeElementCount((**it_).header->type, (**it_).payload);
    if (zbi_item_size == 0) {
      ++(*it_);
      continue;
    }

    // Found an item.
    offset_ = 0;
    return *this;
  }

  // Exhausted all ZBI items. Move to an end state.
  it_ = parent_->view_.end();
  offset_ = 0;
  return *this;
}

MemRangeTable::iterator MemRangeTable::iterator::operator++(int) {
  MemRangeTable::iterator old = *this;
  ++*this;
  return old;
}

fitx::result<std::string_view> MemRangeTable::take_error() {
  // Clear out the the underlying zbitl::View's error and also our own.
  fitx::result<View<ByteView>::Error> view_error = view_.take_error();
  fitx::result<std::string_view> table_error = error_;
  error_ = fitx::ok();

  // Return any error encountered by the the underlying zbitl::View
  // implementation first.
  if (view_error.is_error()) {
    return fitx::error(view_error.error_value().zbi_error);
  }

  // Otherwise, return our own.
  return table_error;
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
