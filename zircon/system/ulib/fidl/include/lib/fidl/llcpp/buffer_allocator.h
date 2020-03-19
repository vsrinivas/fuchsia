// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_BUFFER_ALLOCATOR_H_
#define LIB_FIDL_LLCPP_BUFFER_ALLOCATOR_H_

#include "allocator.h"
#include "tracking_ptr.h"

namespace fidl {

// BufferAllocator allocates objects from its internal contiguous region of memeory.
// The NBytes template parameter specifies the size of the internal buffer.
// Because the buffer is internal, if BufferAllocator is stored on the stack, objects allocated
// with it will also be stored on the stack and no heap allocations will be made.
//
// Usage:
// BufferAllocator<2048> allocator;
// tracking_ptr<MyObj> obj = allocator.make<MyObj>(arg1, arg2);
// tracking_ptr<int[]> arr = allocator.make<int[]>(10);
template <size_t NBytes>
class BufferAllocator final : public Allocator {
  struct DestructorMetadata;

 public:
  BufferAllocator()
      : next_object_(buf_),
        // Point last_destructor_metadata_ so the first metadata is past the end of the
        // buffer -- this is intentional. The metadata pointer will decrease from there.
        last_destructor_metadata_(reinterpret_cast<DestructorMetadata*>(buf_ + NBytes)) {}

  // Remove copy and move constructors becuase the allocator is inherently meant
  // to create pointers to objects inside of it and moving/copying allocators
  // would change the address.
  BufferAllocator(const BufferAllocator&) = delete;
  BufferAllocator(BufferAllocator&&) = delete;
  BufferAllocator& operator=(const BufferAllocator&) = delete;
  BufferAllocator& operator=(BufferAllocator&&) = delete;

  ~BufferAllocator() {
    DestructorMetadata* end_destructor_metadata =
        reinterpret_cast<DestructorMetadata*>(buf_ + NBytes);
    for (; last_destructor_metadata_ < end_destructor_metadata; last_destructor_metadata_++) {
      assert(last_destructor_metadata_->dtor && "dtor should be set in allocate()");
      last_destructor_metadata_->dtor(buf_ + last_destructor_metadata_->offset,
                                      last_destructor_metadata_->count);
    }
  }

 private:
  // buf_ grows from both ends of the buffer
  // Allocated objects are placed in buf_ in low to high address order.
  // DestructorMetadata is placed in buf_ in high to low address order.
  // buf_ is not zero-initialized to avoid the performance cost.
  alignas(FIDL_ALIGNMENT) uint8_t buf_[NBytes];
  // next_object_ contains the address of the next object that will be allocated.
  // This is equivalent to the end of the current allocated objects region.
  uint8_t* next_object_;
  // last_destructor_metadata_ contains the address of the last destructor metadata entry
  // (or the end of buf_ if there is no destructor metadata entry).
  DestructorMetadata* last_destructor_metadata_;

  struct DestructorMetadata {
    uint32_t offset;
    uint32_t count;
    destructor dtor;
  };

  allocation_result allocate(size_t obj_size, uint32_t count, destructor dtor) override {
    size_t block_size = FIDL_ALIGN(obj_size * count);
    void* block = next_object_;

    if (dtor != trivial_destructor) {
      last_destructor_metadata_--;
      last_destructor_metadata_->offset = static_cast<uint32_t>(next_object_ - buf_);
      last_destructor_metadata_->count = count;
      last_destructor_metadata_->dtor = dtor;
    }

    next_object_ += block_size;

    if (next_object_ > reinterpret_cast<uint8_t*>(last_destructor_metadata_)) {
      // OOM - the two pointers allocating from either end crossed.
      abort();
    }

    return allocation_result{.data = block, .requires_delete = false};
  }
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_BUFFER_ALLOCATOR_H_
