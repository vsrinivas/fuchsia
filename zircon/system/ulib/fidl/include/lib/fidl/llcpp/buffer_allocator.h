// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_BUFFER_ALLOCATOR_H_
#define LIB_FIDL_LLCPP_BUFFER_ALLOCATOR_H_

#include "failover_allocator.h"
#include "unsafe_buffer_allocator.h"

namespace fidl {

// In case we missed any, make it obvious what to change in the client code.
//
// Change BufferAllocator<X> to BufferThenHeapAllocator<X>.
// If necessary, change buffer_allocator.h to buffer_then_heap_allocator.h.
//
// We'd use a template<NBytes> using here, except clang doesn't seem to generate a warning for a
// deprecated template using...
//
// With "-Werror" and "-Wno-error=deprecated-declarations" compiler options, this generates a
// compilation warning if used in client code, not a compilation error (please build that way so
// "deprecated" attribute can be used for its intended purpose without causing build failure).
template <size_t NBytes>
class [[deprecated(
    "BufferAllocator<> is deprecated. "
    "Use buffer_then_heap_allocator.h BufferThenHeapAllocator<NBytes> instead.")]] BufferAllocator
    : public FailoverHeapAllocator<::fidl::UnsafeBufferAllocator<NBytes>>{};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_BUFFER_ALLOCATOR_H_
