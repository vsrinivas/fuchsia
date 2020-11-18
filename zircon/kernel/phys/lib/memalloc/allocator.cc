// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memalloc.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <climits>
#include <cstdint>
#include <cstring>

#include <fbl/algorithm.h>

namespace memalloc {

namespace {

// Return the address of the given object as a uintptr_t.
//
// This is really just a short-hand method of writing
// `reinterpret_cast<uintptr_t>(object)`.
template <typename T>
uintptr_t AddressOf(const T* object) {
  return reinterpret_cast<uintptr_t>(object);
}

// In debug mode, overwrite the given range of memory with a pattern.
//
// May be used to help catch use-after-free and similar errors.
void PoisonMemory(std::byte* base, size_t size) {
#if ZX_DEBUG_ASSERT_IMPLEMENTED
  memset(base, 0xee, size);
#endif
}

}  // namespace

// Return true if node `a` is immediately before node `b`.
bool Allocator::ImmediatelyBefore(const Node* a, const Node* b) {
  ZX_DEBUG_ASSERT(a != nullptr);
  ZX_DEBUG_ASSERT(b != nullptr);
  return AddressOf(a) + a->size == AddressOf(b);
}

std::byte* Allocator::AllocateFromNode(Node* prev, Node* current, size_t desired_size,
                                       size_t alignment) {
  // Given a node, we want to find a region of memory inside of it that is aligned
  // to `alignment` with size at least `desired_size`:
  //
  //            .--- region_start                       region_end ---.
  //            v                                                     v
  //            .-------------+-----------------------+---------------.
  //  prev ---> | current     |                       | new_next      |  --> next
  //            '-------------+-----------------------+---------------'
  //                          ^                       ^
  //                          '- allocation_start     '- allocation_end
  //
  //  In the diagram above, `region_start` and `region_end` are the beginning
  //  and the end of the node, respectively.
  //
  //  If we didn't have alignment constraints, we could just carve off space
  //  from the beginning of the region. However, if the user has requested for
  //  a stricter alignment than what `region_start` provides, we may need to
  //  carve out space in the middle of the region.
  //
  //  We calculate `allocation_start` to be the first correctly aligned
  //  address in the region, and `allocation_end` to be `desired_size` bytes
  //  after that. We can allocate from this node iff the region
  //  [allocation_start, allocation_end) falls within [region_start,
  //  region_end).

  // Get the start/end addresses of this `current`.
  uintptr_t region_start = AddressOf(current);
  uintptr_t region_end = region_start + current->size;
  ZX_DEBUG_ASSERT(region_start < region_end);

  // Get a potential region for this allocation, ensuring that we don't
  // overflow while aligning up or calculating the end result.
  uintptr_t allocation_start = fbl::round_up(region_start, alignment);
  if (allocation_start == 0) {
    // overflow while aligning.
    return nullptr;
  }
  uintptr_t allocation_end = allocation_start + desired_size;
  if (allocation_end < allocation_start) {
    // overflow during add.
    return nullptr;
  }

  // Ensure the allocation is entirely within the region.
  ZX_DEBUG_ASSERT(region_start <= allocation_start);  // implied by calculations above.
  if (allocation_end > region_end) {
    return nullptr;
  }

  // If we have space after our allocation, create a new node between `current`
  // and `next`.
  if (region_end > allocation_end) {
    Node* new_next = reinterpret_cast<Node*>(allocation_end);
    new_next->next = current->next;
    new_next->size = region_end - allocation_end;
    current->next = new_next;
  }

  // If we have space before the allocation, update `current`'s size and
  // next pointer. Otherwise, remove `current` from the list.
  if (region_start < allocation_start) {
    current->size = allocation_start - region_start;
  } else {
    prev->next = current->next;
  }

  return reinterpret_cast<std::byte*>(allocation_start);
}

void Allocator::MergeNodes(Node* a, Node* b) {
  ZX_DEBUG_ASSERT(ImmediatelyBefore(a, b));
  a->size += b->size;
  a->next = b->next;
  PoisonMemory(reinterpret_cast<std::byte*>(b), sizeof(Node));
}

void Allocator::AddRange(std::byte* base, size_t size) {
  // Add a new range of memory into the linked list of nodes.
  //
  // There are several cases we need to deal with, because the new range may
  // be (i) independent of all other nodes; (ii) behind another node; (iii) in
  // front of another node; or (iv) causing two existing nodes to merge into
  // one.
  //
  // We don't attempt to handle the 4 cases directly, but instead simply add
  // the new node in its rightful location, and then merge the next/previous
  // nodes in a second pass if the new node happens to be touching any
  // existing node.

  ZX_ASSERT(base != nullptr);
  ZX_ASSERT(size % kBlockSize == 0);
  ZX_ASSERT(AddressOf(base) % kBlockSize == 0);
  ZX_ASSERT(AddressOf(base) + size >= AddressOf(base));  // ensure range doesn't wrap memory

  // If the region is size 0, we have nothing to do.
  if (size == 0) {
    return;
  }

  // Clear out the memory being added to help catch use-after-free errors.
  PoisonMemory(base, size);

  // Create a new node out of the memory region [base, base + size).
  Node* new_node = reinterpret_cast<Node*>(base);
  new_node->next = nullptr;
  new_node->size = size;

  // The free list is sorted by address of region.
  //
  // Find the two nodes that `new_node` should be placed between.
  Node* prev = &first_;
  Node* next = first_.next;
  while (next != nullptr && AddressOf(new_node) > AddressOf(next)) {
    prev = next;
    next = next->next;
  }

  // Insert the node between `prev` and `next`.
  prev->next = new_node;
  new_node->next = next;

  // The new node may be adjacent to `prev`, or `next`, or both.
  //
  // Merge touching nodes.
  if (next != nullptr && ImmediatelyBefore(new_node, next)) {
    MergeNodes(new_node, next);
  }
  if (prev != nullptr && ImmediatelyBefore(prev, new_node)) {
    MergeNodes(prev, new_node);
  }
}

std::byte* Allocator::Allocate(size_t size, size_t alignment) {
  ZX_ASSERT(size % kBlockSize == 0);
  ZX_ASSERT(fbl::is_pow2(alignment));

  // Just return nullptr on 0-size allocations.
  if (size == 0) {
    return nullptr;
  }

  Node* prev = &first_;
  Node* head = first_.next;

  while (head != nullptr) {
    // Attempt to split this node.
    std::byte* memory = AllocateFromNode(prev, head, size, alignment);
    if (memory != nullptr) {
      return memory;
    }

    // Move to the next node.
    prev = head;
    head = head->next;
  }

  return nullptr;
}

}  // namespace memalloc
