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

// Takes an iterator yielding a sorted list of zbi_mem_range_t items, and merges
// together contiguous ranges of the same type.
template <typename Iterator>
class MemRangeMerger {
 public:
  MemRangeMerger() = default;
  MemRangeMerger(Iterator begin, Iterator end) : begin_(begin), end_(end) {}

  class iterator;
  iterator begin() const { return iterator(begin_, end_); }
  iterator end() const { return iterator(end_, end_); }

  class iterator {
   public:
    iterator(Iterator begin, Iterator end) : it_(begin), next_(begin), end_(end) { Next(); }

    // Iterator traits.
    using iterator_category = std::input_iterator_tag;
    using reference = const zbi_mem_range_t&;
    using value_type = zbi_mem_range_t;
    using pointer = const zbi_mem_range_t*;
    using difference_type = size_t;

    // Equality / inequality.
    bool operator==(const iterator& other) const { return it_ == other.it_; }
    bool operator!=(const iterator& other) const { return !(*this == other); }

    // Return the current element.
    const zbi_mem_range_t& operator*() const {
      ZX_DEBUG_ASSERT_MSG(it_ != end_, "Attempted to dereference 'end' iterator.");
      return current_;
    }
    const zbi_mem_range_t* operator->() {
      ZX_DEBUG_ASSERT_MSG(it_ != end_, "Attempted to dereference 'end' iterator.");
      return &current_;
    }

    // Increment operators: move iterator to next element.
    iterator& operator++() {  // prefix
      Next();
      return *this;
    }
    iterator operator++(int) {  // postfix
      iterator result = *this;
      ++this;
      return result;
    }

   private:
    // Ensure iterator type is valid.
    static_assert(std::is_same_v<zbi_mem_range_t, typename Iterator::value_type>,
                  "Expected an iterator of type zbi_mem_range_t.");

    // Fill `current_` with the next merged range.
    void Next() {
      // If we are at the end, do nothing.
      it_ = next_;
      if (it_ == end_) {
        return;
      }

      // Keep merging entries together until we hit the end of our input
      // or hit a discontinuity.
      current_ = *next_;
      ++next_;
      while (next_ != end_) {
        zbi_mem_range_t next = *next_;
        // Ensure the end of this region is the start of the next.
        if (current_.paddr + current_.length != next.paddr) {
          break;
        }
        // Ensure the type of this region matches the next.
        if (current_.type != next.type) {
          break;
        }
        // Increase the size of this region.
        current_.length += next.length;
        ++next_;
      }
    }

    // The merged memory range.
    zbi_mem_range_t current_;

    // The pair [it_, next_) represent the range of items merged into current_.
    Iterator it_;
    Iterator next_;

    // The end_ iterator of the underlying container.
    Iterator end_;
  };

 private:
  Iterator begin_;
  Iterator end_;
};

}  // namespace zbitl

#endif  // LIB_ZBITL_ITEMS_MEM_CONFIG_H_
