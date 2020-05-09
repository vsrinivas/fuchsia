// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_UNSAFE_BUFFER_ALLOCATOR_H_
#define LIB_FIDL_LLCPP_UNSAFE_BUFFER_ALLOCATOR_H_

#include "allocator.h"
#include "tracking_ptr.h"

namespace fidl {

// UnsafeBufferAllocator allocates objects from its internal contiguous region of memeory.
// The NBytes template parameter specifies the size of the internal buffer.
// Because the buffer is internal, if UnsafeBufferAllocator is stored on the stack, objects
// allocated with it will also be stored on the stack and no heap allocations will be made.
//
// See BufferThenHeapAllocator for an allocator that has an internal buffer but
// falls back to the heap if the internal buffer space is exhausted, instead of
// abort()ing as UnsafeBufferAllocator does.
//
// Direct usage of UnsafeBufferAllocator<NBytes> is discouraged in favor of
// BufferThenHeapAllocator<NBytes>.  The UnsafeBufferAllocator<> may move to fidl::internal in
// future.
//
// Usage:
// UnsafeBufferAllocator<2048> allocator;
// tracking_ptr<MyObj> obj = allocator.make<MyObj>(arg1, arg2);
// tracking_ptr<int[]> arr = allocator.make<int[]>(10);
template <size_t NBytes>
class UnsafeBufferAllocator final : public Allocator {
  struct DestructorMetadata;

 public:
  UnsafeBufferAllocator()
      : next_object_(buf_),
        // Point last_destructor_metadata_ so the first metadata is past the end of the
        // buffer -- this is intentional. The metadata pointer will decrease from there.
        last_destructor_metadata_(reinterpret_cast<DestructorMetadata*>(buf_ + NBytes)) {}
  ~UnsafeBufferAllocator() override { cleanup_destructor_metadata(); }

  // Remove copy and move constructors becuase the allocator is inherently meant
  // to create pointers to objects inside of it and moving/copying allocators
  // would change the address.
  UnsafeBufferAllocator(const UnsafeBufferAllocator&) = delete;
  UnsafeBufferAllocator(UnsafeBufferAllocator&&) = delete;
  UnsafeBufferAllocator& operator=(const UnsafeBufferAllocator&) = delete;
  UnsafeBufferAllocator& operator=(UnsafeBufferAllocator&&) = delete;

  // Reset the object so it can make allocations again. Only use this if you
  // are sure that any previous allocations are no longer living, otherwise
  // there will be use-after-free problems.
  // This does not zero the memory to avoid the performance cost.
  void reset() {
    cleanup_destructor_metadata();

    next_object_ = buf_;
    last_destructor_metadata_ = reinterpret_cast<DestructorMetadata*>(buf_ + NBytes);
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

  void cleanup_destructor_metadata() {
    DestructorMetadata* end_destructor_metadata =
        reinterpret_cast<DestructorMetadata*>(buf_ + NBytes);
    for (; last_destructor_metadata_ < end_destructor_metadata; last_destructor_metadata_++) {
      assert(last_destructor_metadata_->dtor && "dtor should be set in allocate()");
      last_destructor_metadata_->dtor(buf_ + last_destructor_metadata_->offset,
                                      last_destructor_metadata_->count);
    }
  }

  // Check if we have enough space to make the given allocation.
  bool can_allocate(size_t block_size, destructor dtor) const {
    DestructorMetadata* metadata_ptr = last_destructor_metadata_;

    if (dtor != trivial_destructor) {
      metadata_ptr--;
    }
    return ((next_object_ + block_size) <= reinterpret_cast<uint8_t*>(metadata_ptr));
  }

  allocation_result allocate(AllocationType type, size_t obj_size, size_t count,
                             destructor dtor) override {
    assert(count <= static_cast<size_t>(std::numeric_limits<uint32_t>::max()) &&
           "fidl::UnsafeBufferAllocator expects a count that can fit within uint32_t");
    size_t block_size = FIDL_ALIGN(obj_size * count);
    void* block = next_object_;

    if (!can_allocate(block_size, dtor)) {
      // When UnsafeBufferAllocator is not wrapped with FailoverHeapAllocator, this requests that
      // fidl::Allocator ZX_PANIC() the whole process.  Consider using
      // BufferThenHeapAllocator<NBytes> instead of UnsafeBufferAllocator<NBytes> to wrap in
      // FailoverHeapAllocator<> and avoid the potential ZX_PANIC().
      return allocation_result{.data = nullptr, .heap_allocate = false};
    }

    if (dtor != trivial_destructor) {
      last_destructor_metadata_--;
      last_destructor_metadata_->offset = static_cast<uint32_t>(next_object_ - buf_);
      last_destructor_metadata_->count = static_cast<uint32_t>(count);
      last_destructor_metadata_->dtor = dtor;
    }

    next_object_ += block_size;

    return allocation_result{.data = block, .heap_allocate = false};
  }
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_UNSAFE_BUFFER_ALLOCATOR_H_
