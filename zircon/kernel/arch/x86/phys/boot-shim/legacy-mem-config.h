// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_LEGACY_MEM_CONFIG_H_
#define ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_LEGACY_MEM_CONFIG_H_

#include <lib/fit/result.h>
#include <lib/stdcompat/span.h>
#include <lib/zbitl/storage-traits.h>
#include <lib/zircon-internal/e820.h>
#include <zircon/boot/image.h>

#include <iterator>
#include <string_view>
#include <variant>

#include <efi/boot-services.h>

// E820 memory table, an array of E820Entry.
constexpr uint32_t kLegacyZbiTypeE820Table = 0x30323845;  // E820

// EFI memory map, a uint64_t entry size followed by a sequence of
// EFI memory descriptors aligned on that entry size.
constexpr uint32_t kLegacyZbiTypeEfiMemoryMap = 0x4d494645;  // EFIM

namespace internal {

// A view into a ZBI_TYPE_MEM_CONFIG payload.
using MemConfigTable = cpp20::span<const zbi_mem_range_t>;

// A view into a kLegacyZbiTypeE820Table payload.
using E820Table = cpp20::span<const E820Entry>;

// A view into a kLegacyZbiTypeEfiMemoryMap payload.
struct EfiTable {
  size_t num_entries;
  size_t entry_size;
  zbitl::ByteView payload;
};

zbi_mem_range_t ToMemRange(const E820Entry& range);
zbi_mem_range_t ToMemRange(const efi_memory_descriptor& range);

}  // namespace internal

// MemRangeTable allows iterating over all memory ranges specified in a
// given ZBI item.
//
// Memory ranges may be represented in multiple input formats in the
// ZBI. The class allows the various formats to be uniformly handled.
//
// The iterator can be used as follows:
//
// ```
// MemRangeTable container = MemRangeTable::FromItem(item);
//
// // Process all the items.
// for (zbi_mem_range_t range : container) {
//   Process(range);
// }
// ```
class MemRangeTable {
 public:
  // Create a MemRangeTable from the given memory range, assumed to be of type `zbi_type`.
  static fit::result<std::string_view, MemRangeTable> FromSpan(uint32_t zbi_type,
                                                               zbitl::ByteView payload);

  // begin/end iterators over the items in the table.
  class iterator;
  iterator begin() const { return iterator(this, 0); }
  iterator end() const { return iterator(this, size()); }

  // Return the number of memory ranges in the table.
  size_t size() const;

  // Get the n'th item. Input must be strictly less than the result of `size()`.
  zbi_mem_range_t operator[](size_t n) const;

  class iterator {
   public:
    iterator() = default;

    // Iterator traits.
    using iterator_category = std::input_iterator_tag;
    using reference = const zbi_mem_range_t&;
    using value_type = zbi_mem_range_t;
    using pointer = const zbi_mem_range_t*;
    using difference_type = size_t;

    // Equality / inequality.
    bool operator==(const iterator& other) const;
    bool operator!=(const iterator& other) const { return !(*this == other); }

    // Return the current element.
    zbi_mem_range_t operator*() const;

    // Increment operators: move iterator to next element.
    iterator& operator++() {  // prefix
      ++offset_;
      return *this;
    }
    iterator operator++(int) {  // postfix
      MemRangeTable::iterator old = *this;
      ++*this;
      return old;
    }

   private:
    friend class MemRangeTable;
    iterator(const MemRangeTable* parent, size_t offset) : parent_(parent), offset_(offset) {}

    // Parent MemRangeTable.
    const MemRangeTable* parent_ = nullptr;

    // The current offset of the current payload.
    size_t offset_ = 0;
  };

 private:
  std::variant<internal::MemConfigTable, internal::E820Table, internal::EfiTable> table_;
};

#endif  // ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_LEGACY_MEM_CONFIG_H_
