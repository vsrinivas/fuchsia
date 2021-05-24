// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_ALLOCATOR_H_
#define ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_ALLOCATOR_H_

#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>

#include <cstddef>
#include <optional>

#include <fbl/intrusive_double_list.h>

namespace memalloc {

struct RangeNode;
class RangeStorage;

// The range of uint64_t values [first, last].
//
// The [base, base + length) is generally more convenient to work with, but
// can't represent the range [0, UINT64_MAX]. We thus expose the latter on the
// API, but use for the former as our internal representation.
struct Range {
  uint64_t first;
  uint64_t last;

  // Create a range with the given first/last pair.
  static Range FromFirstAndLast(uint64_t first, uint64_t last) {
    Range range;
    range.first = first;
    range.last = last;
    return range;
  }

  // Equality / inequality.
  friend bool operator==(const Range& lhs, const Range& rhs) {
    return lhs.first == rhs.first && lhs.last == rhs.last;
  }
  friend bool operator!=(const Range& lhs, const Range& rhs) { return !(lhs == rhs); }
};

// A node in the allocator's internal free and used lists.
struct RangeNode
    : public fbl::DoublyLinkedListable<RangeNode*, fbl::NodeOptions::AllowCopyMove |
                                                       fbl::NodeOptions::AllowClearUnsafe> {
  Range range;
};

// Storage space for Range.
//
// Allows callers to allocate storage space for Range objects, which in turn
// can be passed into the allocator. Allocating Range objects directly can be
// problematic due to their destructor, which may cause problems with
// statically allocated objects that are destructed at program shutdown.
//
// RangeStorage allocates space without the caller needing to worry about
// object construction or destruction.
class RangeStorage {
 public:
  RangeStorage() = default;

  // Disallow move/copy.
  RangeStorage(const RangeStorage&) = delete;
  RangeStorage& operator=(const RangeStorage&) = delete;

  // Get the internal storage as a range pointer.
  RangeNode* AsRangeNode() { return reinterpret_cast<RangeNode*>(storage_); }

 private:
  alignas(RangeNode) unsigned char storage_[sizeof(RangeNode)];
};

// A range allocator class.
//
// Space for book-keeping is provided by the caller during construction, via
// the "cpp20::span<RangeStorage>" parameter. One RangeStory entry is used for
// every non-contiguous range tracked by the allocator:
//
//   // Create an allocator that can store up to 100 ranges.
//   RangeStorage storage[100];
//   Allocator allocator(storage);
//
// The book-keeping memory must outlive the class.
//
// Ranges may be freely added and removed from the allocator. Newly added
// ranges may freely overlap previously added ranges, and it is safe to remove
// ranges that are not currently tracked by the allocator:
//
//   allocator.AddRange(1, 100);      // Add the range [1, 101)
//   allocator.RemoveRange(50, 200);  // Remove the second half, leaving the range [1, 50).
//
class Allocator {
 public:
  ~Allocator();

  // Create a new allocator, using the given span for book keeping.
  //
  // The memory at `nodes` must outlive this class instance.
  explicit Allocator(cpp20::span<RangeStorage> storage);

  // Prevent copy / assign.
  Allocator(const Allocator&) = delete;
  Allocator& operator=(const Allocator&) = delete;

  // Add the given range to the allocator.
  //
  // Ranges or parts of ranges already added to the allocator may be safely
  // added again. May fail with ZX_ERR_NO_MEMORY if insufficient
  // book-keeping space is available.
  //
  // Adding a range is O(n) in the number of ranges tracked.
  zx::status<> AddRange(uint64_t base, uint64_t size);

  // Remove the given range from the allocator.
  //
  // Ranges not previously added may safely be removed. May fail with
  // ZX_ERR_NO_MEMORY if insufficient book-keeping space is available.
  //
  // Removing a range is O(n) in the number of ranges tracked.
  zx::status<> RemoveRange(uint64_t base, uint64_t size);

  // Allocate a range of the given size and alignment.
  //
  // Returns the base of an allocated range of the given size if successful.
  //
  // Returns ZX_ERR_NO_RESOURCES if there was no range found that could
  // satisfy the request.
  //
  // Returns ZX_ERR_NO_MEMORY if a range could be found, but there was
  // insufficient book-keeping memory to track it.
  //
  // Allocation is O(n) in the number of ranges tracked.
  zx::status<uint64_t> Allocate(uint64_t size, uint64_t alignment = 1);

  // Iterate through ranges currently available in the allocator.
  //
  // Ranges will be returned in order, with contiguous ranges merged.
  class iterator;
  iterator begin() const { return iterator(ranges_.cbegin()); }
  iterator end() const { return iterator(ranges_.cend()); }

  // An iterator that enumerates a list of Ranges.
  class iterator {
   public:
    iterator() = default;
    iterator(const iterator& other) = default;

    // Iterator traits.
    using iterator_category = std::input_iterator_tag;
    using reference = const Range&;
    using value_type = Range;
    using pointer = const Range*;
    using difference_type = size_t;

    // Equality / inequality.
    bool operator==(const iterator& other) const { return it_ == other.it_; }
    bool operator!=(const iterator& other) const { return !(*this == other); }

    // Return the current element.
    const Range& operator*() const { return it_->range; }
    const Range* operator->() const { return &(it_->range); }

    // Increment operators: move iterator to next element.
    iterator& operator++() {  // prefix
      ++it_;
      return *this;
    }
    iterator operator++(int) {  // postfix
      Allocator::iterator old = *this;
      ++*this;
      return old;
    }

   private:
    friend Allocator;
    explicit iterator(fbl::DoublyLinkedList<memalloc::RangeNode*>::const_iterator it) : it_(it) {}

    // Parent table iterator.
    fbl::DoublyLinkedList<memalloc::RangeNode*>::const_iterator it_;
  };

 private:
  using RangeIter = fbl::DoublyLinkedList<RangeNode*>::iterator;

  // Remove the given range [first, last] from `node`.
  //
  // [first, last] must fall entirely within `node`.
  zx::status<> RemoveRangeFromNode(RangeIter node, uint64_t first, uint64_t last);

  // Combine two consecutive nodes `a` and `b` into a single node,
  // deallocating `b`.
  void MergeRanges(RangeIter a, RangeIter b);

  // Attempt to allocate a range of size `desired_size` with the given
  // `alignment` out of `node`.
  //
  // If successful, return the base of the allocated range. If there is not
  // enough space in this node, returns ZX_ERR_NEXT. If insufficient memory
  // was available for book-keeping, returns ZX_ERR_NO_MEMORY.
  //
  // `desired_size` must be at least 1.
  zx::status<uint64_t> TryToAllocateFromNode(RangeIter node, uint64_t desired_size,
                                             uint64_t alignment);

  // Allocate a Range node from the internal free list, with the given
  // first/last values.
  RangeNode* CreateRangeNode(uint64_t first, uint64_t last);

  // Put the given Range Node back on the internal free list.
  void FreeRangeNode(RangeNode* range);

  // List of nodes, sorted by the beginning of the range.
  fbl::DoublyLinkedList<RangeNode*> ranges_;

  // List of unused nodes.
  fbl::DoublyLinkedList<RangeNode*> free_list_;
};

}  // namespace memalloc

#endif  // ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_ALLOCATOR_H_
