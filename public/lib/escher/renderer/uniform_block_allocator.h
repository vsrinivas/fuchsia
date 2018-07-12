// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_UNIFORM_BLOCK_ALLOCATOR_H_
#define LIB_ESCHER_RENDERER_UNIFORM_BLOCK_ALLOCATOR_H_

#include <vector>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/renderer/uniform_allocation.h"

namespace escher {

// Similar to BlockAllocator, except that it returns suballocations from within
// fixed-size GPU uniform buffers.  A UniformBufferPool is used to obtain the
// buffers that are allocated from.
class UniformBlockAllocator {
 public:
  // The pool must be guaranteed to outlive the allocator.
  explicit UniformBlockAllocator(impl::UniformBufferPoolWeakPtr pool);

  UniformAllocation Allocate(size_t size, size_t alignment);

  // Invalidates all previously-allocated pointers, and frees memory for reuse.
  // The freed buffers are immediately released back to the UniformBufferPool.
  void Reset();

  // Allows the caller to manage the valid lifetime of the suballocation
  // buffers, which will be returned to the UniformBufferPool when their
  // ref-count reaches zero.  For example, if the returned buffers are
  // immediately freed then the behavior is effectively identical to Reset().
  //
  // NOTE: the caller is responsible for returning the buffers to the pool
  // before the pool is destroyed (this is enforced by ResourceManager DCHECKs).
  // The caller must therefore know something about the lifetime of the pool (or
  // at least the lifetime of this allocator, since the pool is required to
  // outlive this allocator).
  std::vector<BufferPtr> TakeBuffers();

 private:
  void Reserve();

  const impl::UniformBufferPoolWeakPtr pool_;
  const vk::DeviceSize buffer_size_;
  std::vector<BufferPtr> buffers_;
  size_t write_index_;
};

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_UNIFORM_BLOCK_ALLOCATOR_H_
