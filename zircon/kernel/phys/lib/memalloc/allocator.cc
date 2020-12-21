// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memalloc.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <algorithm>

#include <fbl/intrusive_double_list.h>
#include <fbl/span.h>

namespace memalloc {

namespace {

// Return an incremented copy of `iterator`.
//
// Similar to `std::next`, but doesn't require the C++17 iterator traits
// to be implemented on `iterator`.
template <typename T>
T Next(T iterator) {
  T copy = iterator;
  ++copy;
  return copy;
}

// Return true if the two given ranges overlap.
bool RangesIntersect(const Range& a, const Range& b, uint64_t* first = nullptr,
                     uint64_t* last = nullptr) {
  // Does "a" lie entirely before "b"?
  if (a.last < b.first) {
    return false;
  }

  // Does "b" lie entirely before "a"?
  if (b.last < a.first) {
    return false;
  }

  if (first) {
    *first = std::max(a.first, b.first);
  }
  if (last) {
    *last = std::min(a.last, b.last);
  }
  return true;
}

// Return true if the end of range `a` is immeadiately before the start of range `b`.
bool ImmediatelyBefore(const Range& a, const Range& b) {
  return b.first > 0 && a.last == b.first - 1;
}

// Return true if the two ranges overlap or are touching.
bool RangesConnected(const Range& a, const Range& b) {
  return ImmediatelyBefore(a, b) || ImmediatelyBefore(b, a) || RangesIntersect(a, b);
}

}  // namespace

Allocator::Allocator(fbl::Span<Range> nodes) {
  for (Range& node : nodes) {
    free_list_.push_front(new (&node) Range{});
  }
}

Allocator::~Allocator() {
  free_list_.clear();
  ranges_.clear();
}

zx::status<> Allocator::RemoveRangeFromNode(RangeIter node, uint64_t first, uint64_t last) {
  // We want to allocate a given range from inside of the node `current`:
  //
  //      .--- current.first                  current.last ---.
  //      v                                                   v
  //     .-------------+-----------------------+---------------.
  //     |             |###### allocation #####|               |
  //     '-------------+-----------------------+---------------'
  //                    ^                     ^
  //                    '- first              '- last
  //
  // In the diagram above, `current.first` and `current.last` are the beginning
  // and the last of the node containing the range, respectively
  //
  // `first` and `last` may be the full range or just a subrange of it. If it
  // happens to be in the middle of the current node's range, we will end up with
  // one more range node in the list than what we firsted with.

  // Ensure inputs are ordered.
  ZX_DEBUG_ASSERT(first <= last);

  // If the range doesn't overlap the node at all, we have nothing to do.
  if (!RangesIntersect(*node, Range::FromFirstAndLast(first, last))) {
    return zx::ok();
  }

  // If the requested allocation covers the whole range, just delete this node.
  if (first <= node->first && last >= node->last) {
    FreeRangeNode(ranges_.erase(*node));
    return zx::ok();
  }

  // If the allocation is at the beginning of this node, just adjust the node's
  // starting point.
  if (first <= node->first) {
    node->first = last + 1;
    return zx::ok();
  }

  // If the allocation is at the end of this node, just adjust the size.
  if (last >= node->last) {
    node->last = first - 1;
    return zx::ok();
  }

  // Otherwise, the allocation is in the middle. Update the node to
  // represent the space at the beginning, and allocate a new node for the space
  // at the end.
  Range* new_next = CreateRangeNode(last + 1, node->last);
  if (new_next == nullptr) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  node->last = first - 1;
  ranges_.insert_after(node, new_next);
  return zx::ok();
}

zx::status<uint64_t> Allocator::TryToAllocateFromNode(RangeIter node, uint64_t desired_size,
                                                      uint64_t alignment) {
  ZX_DEBUG_ASSERT(desired_size > 0);

  // Get a potential region for this allocation, ensuring that we don't
  // overflow while aligning up or calculating the last result.
  uint64_t allocation_first = fbl::round_up(node->first, alignment);
  if (node->first != 0 && allocation_first == 0) {
    // Overflow while aligning.
    return zx::error(ZX_ERR_NEXT);
  }
  uint64_t allocation_last = allocation_first + desired_size - 1;
  if (allocation_last < allocation_first) {
    // Overflow during add.
    return zx::error(ZX_ERR_NEXT);
  }

  // Determine if the proposed allocation can fit in this node's range.
  ZX_DEBUG_ASSERT(node->first <= allocation_first);  // implied by calculations above.
  if (allocation_last > node->last) {
    return zx::error(ZX_ERR_NEXT);
  }

  // Allocate the node.
  zx::status<> result = RemoveRangeFromNode(node, allocation_first, allocation_last);
  if (result.is_error()) {
    return result.take_error();
  }
  return zx::ok(allocation_first);
}

void Allocator::MergeRanges(RangeIter a, RangeIter b) {
  ZX_DEBUG_ASSERT(RangesConnected(*a, *b));
  a->first = std::min(a->first, b->first);
  a->last = std::max(a->last, b->last);
  FreeRangeNode(ranges_.erase(b));
}

Range* Allocator::CreateRangeNode(uint64_t first, uint64_t last) {
  Range* node = free_list_.pop_front();
  node->first = first;
  node->last = last;
  return node;
}

void Allocator::FreeRangeNode(Range* range) {
  ZX_DEBUG_ASSERT(!range->InContainer());
  free_list_.push_front(range);
}

zx::status<> Allocator::AddRange(uint64_t base, uint64_t size) {
  // Add a new range of memory into the linked list of nodes.
  //
  // There are several cases we need to deal with, such as (partially)
  // overlapping nodes or a new range causing to existing nodes to be merged
  // into one.
  //
  // We don't attempt to handle the cases directly, but instead simply add
  // the new node in its rightful location, and then merge all nodes in
  // a second pass.

  // If the region is size 0, we have nothing to do.
  if (size == 0) {
    return zx::ok();
  }

  // Ensure the range doesn't overflow.
  uint64_t last = base + size - 1;
  ZX_ASSERT(base <= last);

  // Create a new range node for the range.
  Range* new_range = CreateRangeNode(base, last);
  if (new_range == nullptr) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  // The free list is sorted by address of region. Insert the new node in the
  // correctly sorted location.
  auto prev = ranges_.end();  // we use "end" to mean "no previous node".
  auto it = ranges_.begin();
  while (it != ranges_.end() && new_range->first > it->first) {
    prev = it;
    ++it;
  }
  // Insert `new_range` before `it`. If `it == ranges_.end()`, then this
  // simply adds it to the end of list.
  ranges_.insert(it, new_range);
  auto new_range_it = ranges_.make_iterator(*new_range);

  // The new range may be touching the previous range. If so, merge them
  // together.
  if (prev != ranges_.end() && RangesConnected(*prev, *new_range_it)) {
    MergeRanges(prev, new_range_it);
    new_range_it = prev;
  }

  // The new range may be touching or overlapping any number of subsequent
  // ranges. Keep merging the ranges together there is no more overlap.
  auto next = Next(new_range_it);
  while (next != ranges_.end() && RangesConnected(*new_range_it, *next)) {
    MergeRanges(new_range_it, next);
    next = Next(new_range_it);
  }

  return zx::ok();
}

zx::status<> Allocator::RemoveRange(uint64_t base, uint64_t size) {
  // If the range to remove is size 0, we have nothing to do.
  if (size == 0) {
    return zx::ok();
  }

  // Ensure the range doesn't overflow.
  const uint64_t range_first = base;
  const uint64_t range_last = base + size - 1;
  ZX_ASSERT(range_first <= range_last);

  // Iterate through the free list, trimming anything that intersects with the
  // desired range.
  //
  // Stop when we get to the end, or we start seeing nodes that start after
  // our removed range finishes.
  auto it = ranges_.begin();
  while (it != ranges_.end() && it->first <= range_last) {
    auto current = it++;
    zx::status<> result = RemoveRangeFromNode(current, range_first, range_last);
    if (result.is_error()) {
      return result.take_error();
    }
  }

  return zx::ok();
}

zx::status<uint64_t> Allocator::Allocate(uint64_t size, uint64_t alignment) {
  ZX_ASSERT(fbl::is_pow2(alignment));

  // Return 0 on 0-size allocations.
  if (size == 0) {
    return zx::ok(0);
  }

  // Search through all ranges, attempting to allocate from each one.
  for (auto it = ranges_.begin(); it != ranges_.end(); ++it) {
    // Attempt to allocate from this node.
    zx::status<uint64_t> result = TryToAllocateFromNode(it, size, alignment);
    if (result.is_ok()) {
      return result.take_value();
    }

    // ZX_ERR_NEXT indicates we should keep searching. If we got anything
    // else, give up.
    if (result.error_value() != ZX_ERR_NEXT) {
      return result.take_error();
    }
  }

  // No range could satisfy the allocation.
  return zx::error(ZX_ERR_NO_RESOURCES);
}

}  // namespace memalloc
