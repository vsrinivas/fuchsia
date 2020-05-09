// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_BUFFER_THEN_HEAP_ALLOCATOR_H_
#define LIB_FIDL_LLCPP_BUFFER_THEN_HEAP_ALLOCATOR_H_

#include "failover_allocator.h"
#include "unsafe_buffer_allocator.h"

namespace fidl {

// BufferThenHeapAllocator allocates objects from its internal contiguous region of memory, or if
// that internal memory is exhausted, from the heap.
//
// The NBytes template parameter specifies the size of the internal buffer.
//
// If BufferThenHeapAllocator is stored on the stack and objects all fit within NBytes including
// destructor tracking overhead, objects allocated with it will also be stored on the stack and
// heap allocations will not be made.
//
// When setting NBytes, please set a size that comfortably fits on the stack.  Over-use of stack
// can lead to stack exhaustion which crashes the process.  It's better to set a smaller NBytes
// and fail over to heap sometimes than to cause stack exhaustion.  NBytes of 512 tends to be
// ok assuming very limited recursion.
//
// At NBytes > 2048, consider putting the whole BufferThenHeapAllocator<> on the heap (where it'll
// do one heap allocation instead of many, assuming everything fits).
//
// Consider using HeapAllocator for paths which aren't performance sensitive.
//
// If you need allocations to out-live the allocator that was used to make them, use HeapAllocator
// instead.
//
// Usage:
// BufferThenHeapAllocator<2048> allocator;
// tracking_ptr<MyObj> obj = allocator.make<MyObj>(arg1, arg2);
// tracking_ptr<int[]> arr = allocator.make<int[]>(10);
// // succeeds, but ends up in separate independent heap allocation:
// tracking_ptr<uint8_t[]> arr2 = allocator.make<uint8_t[]>(2 * 2048);
template <size_t NBytes>
using BufferThenHeapAllocator = FailoverHeapAllocator<::fidl::UnsafeBufferAllocator<NBytes>>;

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_BUFFER_THEN_HEAP_ALLOCATOR_H_
