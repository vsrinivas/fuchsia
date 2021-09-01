// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_ITEMS_MEM_CONFIG_H_
#define LIB_ZBITL_ITEMS_MEM_CONFIG_H_

#include <lib/fitx/result.h>
#include <lib/zbitl/items/internal/mem_range_types.h>
#include <lib/zbitl/view.h>
#include <zircon/boot/image.h>

#include <iterator>
#include <variant>

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
    static_assert(
        std::is_same_v<zbi_mem_range_t, typename std::iterator_traits<Iterator>::value_type>,
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

/// This returns the human-readable name for this zbi_mem_range_t.type value.
/// It returns the default-constructed (empty()) string_view for unknown types.
std::string_view MemRangeTypeName(uint32_t type);

}  // namespace zbitl

#endif  // LIB_ZBITL_ITEMS_MEM_CONFIG_H_
