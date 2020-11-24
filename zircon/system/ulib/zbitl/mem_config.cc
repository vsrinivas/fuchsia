// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mem_config.h"

#include <inttypes.h>
#include <lib/zbitl/items/mem_config.h>
#include <lib/zbitl/view.h>
#include <lib/zx/status.h>
#include <stdio.h>
#include <zircon/boot/e820.h>
#include <zircon/boot/image.h>
#include <zircon/limits.h>

#include <efi/boot-services.h>
#include <fbl/span.h>

namespace zbitl {
namespace {

using zbitl::internal::ToMemRange;

// Reinterpret the given ByteView as span of type T.
template <typename T>
fbl::Span<const T> ByteViewAsSpan(ByteView bytes) {
  return fbl::Span<const T>(reinterpret_cast<const T*>(bytes.data()), bytes.size() / sizeof(T));
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
      return payload.size() / sizeof(efi_memory_descriptor);
    default:
      return 0;
  }
}

// Get the n'th element from the given ZBI payload.
zbi_mem_range_t MemRangeElement(uint32_t type, ByteView payload, size_t n) {
  switch (type) {
    case ZBI_TYPE_E820_TABLE:
      return ToMemRange(ByteViewAsSpan<e820entry_t>(payload)[n]);
    case ZBI_TYPE_MEM_CONFIG:
      return ByteViewAsSpan<zbi_mem_range_t>(payload)[n];
    case ZBI_TYPE_EFI_MEMORY_MAP:
      return ToMemRange(ByteViewAsSpan<efi_memory_descriptor>(payload)[n]);
    default:
      ZX_PANIC("Attempted to get element of non-memory payload type %" PRIx32 "\n", type);
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
  // If we have a default-constructed ZBI, just return an empty iterator.
  if (view_.storage().empty()) {
    return end();
  }

  // Otherwise, begin iteration.
  return MemRangeTable::iterator(this);
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
    size_t zbi_item_size = MemRangeElementCount((**it_).header->type, (**it_).payload);
    if (zbi_item_size > 0) {
      offset_ = 0;
      return *this;
    }
    ++(*it_);
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

fitx::result<zbitl::View<ByteView>::Error> MemRangeTable::take_error() {
  return view_.take_error();
}

}  // namespace zbitl
