// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_H_
#define ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_H_

#include <cstddef>

#include <fbl/algorithm.h>

namespace memalloc {

class Allocator {
 public:
  // Create a new, empty allocator.
  Allocator() = default;

  // Prevent copy / assign.
  Allocator(const Allocator&) = delete;
  Allocator& operator=(const Allocator&) = delete;

  // Add a range of memory to the allocator.
  //
  // The range must be unused, and not already managed by the allocator.
  //
  // Both `base` and `size` must be aligned to kBlockSize.
  void AddRange(std::byte* base, size_t size);

  // Allocate memory of the given size and the given alignment.
  //
  // Return nullptr if there was insufficient contiguous memory to perform the allocation.
  [[nodiscard]] std::byte* Allocate(size_t size, size_t alignment = kBlockSize);

  // Internal size of blocks used by the allocator.
  //
  // All allocations sizes and alignment must be aligned to kBlockSize.
  static constexpr size_t kBlockSize = sizeof(uint64_t) * 4;

 private:
  // Linked list node.
  struct Node {
    Node* next;
    size_t size;
  };

  // We need every free region to be large enough to store a Node structure at
  // the beginning, and aligned to at least alignof(Node).
  //
  // Requiring that all ranges of memory and allocation sizes are aligned to
  // at least sizeof(Node) ensures this.
  static_assert(kBlockSize >= sizeof(Node));
  static_assert(kBlockSize >= alignof(Node));
  static_assert(fbl::is_pow2(kBlockSize));

  // Return true if node `a` is immediately before node `b`, with no gap between the two.
  static bool ImmediatelyBefore(const Node* a, const Node* b);

  // Combine two consecutive nodes `a` and `b` into a single node.
  static void MergeNodes(Node* a, Node* b);

  // Attempt to allocate `desired_size` bytes of memory out the node `current`.
  //
  // If successful, return the address of the allocated range of memory and
  // update the free list to reflect the new allocation. If there is not enough
  // space in this node, returns nullptr.
  static std::byte* AllocateFromNode(Node* prev, Node* current, size_t desired_size,
                                     size_t alignment);

  // List of nodes, sorted by address of the node, with the exception of
  // the first sentinel node below.
  Node first_ = {.next = nullptr, .size = 0};
};

}  // namespace memalloc

#endif  // ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_H_
