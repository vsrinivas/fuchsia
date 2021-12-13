// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_ITEMS_MEM_CONFIG_H_
#define LIB_ZBITL_ITEMS_MEM_CONFIG_H_

#include <lib/fitx/result.h>
#include <lib/zbitl/view.h>
#include <zircon/boot/image.h>

#include <iterator>
#include <variant>

#include "internal/mem-range-types.h"

namespace zbitl {

// MemRangeTable allows iterating over all memory ranges specified in a
// given ZBI item.
//
// Memory ranges may be represented in multiple input formats in the
// ZBI. The class allows the various formats to be uniformly handled.
//
// The iterator can be used as follows:
//
// ```
// zbitl::MemRangeTable container = zbitl::MemRangeTable::FromItem(item);
//
// // Process all the items.
// for (zbi_mem_range_t range : container) {
//   Process(range);
// }
// ```
class MemRangeTable {
 public:
  // Create a MemRangeTable from the given ZBI view.
  //
  // If the View contains multiple memory range tables, the last is used.
  static fitx::result<std::string_view, MemRangeTable> FromView(View<ByteView> view);

  // Create a MemRangeTable from the given ZBI item.
  static fitx::result<std::string_view, MemRangeTable> FromItem(View<ByteView>::iterator it);

  // Create a MemRangeTable from the given memory range, assumed to be of type `zbi_type`.
  static fitx::result<std::string_view, MemRangeTable> FromSpan(uint32_t zbi_type,
                                                                ByteView payload);

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

}  // namespace zbitl

#endif  // LIB_ZBITL_ITEMS_MEM_CONFIG_H_
