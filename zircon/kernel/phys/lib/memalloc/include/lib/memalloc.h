// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_H_
#define ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_H_

#include <lib/zx/status.h>

#include <cstddef>
#include <optional>

#include <fbl/intrusive_double_list.h>
#include <fbl/span.h>

namespace memalloc {

// The range of uint64_t values [first, last].
//
// The [base, base + length) is generally more convenient to work with, but
// can't represent the range [0, UINT64_MAX]. We thus expose the latter on the
// API, but use for the former as our internal representation.
struct Range : public fbl::DoublyLinkedListable<Range*> {
  uint64_t first;
  uint64_t last;
};

// A range allocator class.
//
// Space for book-keeping is provided by the caller during construction, via
// the "fbl::Span<Range>" parameter. One entry is used for every
// non-contiguous range tracked by the allocator.
//
// Ranges may be freely added and removed from the allocator. Newly added
// ranges may freely overlap previously added ranges, and it is safe to remove
// ranges that are not currently tracked by the allocator.
//
// The book-keeping memory must outlive the class.
class Allocator {
 public:
  ~Allocator();

  // Create a new allocator, using the given span for book keeping.
  //
  // The memory at `nodes` must outlive this class instance.
  explicit Allocator(fbl::Span<Range> nodes);

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

 private:
  using RangeIter = fbl::DoublyLinkedList<Range*>::iterator;

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
  Range* CreateRangeNode(uint64_t first, uint64_t last);

  // Put the given Range Node back on the internal free list.
  void FreeRangeNode(Range* range);

  // List of nodes, sorted by the beginning of the range.
  fbl::DoublyLinkedList<Range*> ranges_;

  // List of unused nodes.
  fbl::DoublyLinkedList<Range*> free_list_;
};

}  // namespace memalloc

#endif  // ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_H_
