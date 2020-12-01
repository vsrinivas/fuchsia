// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_ITEMS_MEM_CONFIG_H_
#define LIB_ZBITL_ITEMS_MEM_CONFIG_H_

#include <lib/fitx/result.h>
#include <lib/zbitl/view.h>
#include <zircon/boot/image.h>

#include <variant>

namespace zbitl {

// MemRangeTable allows iterating over all memory ranges specified in a
// given ZBI.
//
// Memory ranges may be represented in multiple input formats in the ZBI,
// and may also be spread across multiple ZBI items. The class will allow
// all such items to be iterated over in order.
//
// The iterator can be used as follows:
//
// ```
// zbitl::View<zbitl::ByteView> zbi = ...;
// zbitl::MemRangeTable container{zbi};
//
// // Process all the items.
// for (zbi_mem_range_t range : container) {
//   Process(range);
// }
//
// // Check for errors: must be done before container destruction.
// if (auto result = container.take_error(); result.is_error()) {
//   // ...
// }
// ```
//
// If a ZBI contains multiple different items specifying physical memory
// ranges, the iterator will iterate through all of them.
class MemRangeTable {
 public:
  MemRangeTable();
  explicit MemRangeTable(View<ByteView> view);

  MemRangeTable(const MemRangeTable&) = default;
  MemRangeTable(MemRangeTable&&) = default;

  class iterator;
  iterator begin();
  iterator end();

  // Return the number of memory ranges in the table, or an error if the input
  // ZBI is invalid.
  //
  // O(n) in the number of entries in the ZBI, but more efficient than
  // iterating over every entry, which would be O(n + m) where "m" is the
  // number of ranges.
  fitx::result<zbitl::View<ByteView>::Error, size_t> size() const;

  // Return any error encountered during ZBI iteration.
  //
  // Must always be called prior to object destruction.
  fitx::result<zbitl::View<ByteView>::Error> take_error();

  class iterator {
   public:
    iterator() = default;
    iterator(const iterator& other) = default;

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
    iterator& operator++();    // prefix
    iterator operator++(int);  // postfix

   private:
    friend class MemRangeTable;

    explicit iterator(MemRangeTable* parent);
    iterator(MemRangeTable* parent, View<ByteView>::iterator it, size_t offset);

    // Parent table.
    MemRangeTable* parent_ = nullptr;

    // The current payload we are returning results on.
    //
    // Invariant: `it_` is either nullopt, or is a valid non-end iterator into parent_->view_.
    std::optional<View<ByteView>::iterator> it_ = std::nullopt;

    // The current offset of the current payload.
    size_t offset_ = 0;
  };

 private:
  View<ByteView> view_;
};

}  // namespace zbitl

#endif  // LIB_ZBITL_ITEMS_MEM_CONFIG_H_
