// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/trivial-allocator/basic-leaky-allocator.h>
#include <lib/trivial-allocator/basic-owning-allocator.h>
#include <lib/trivial-allocator/page-allocator.h>
#include <lib/trivial-allocator/sealed-page-allocator.h>

#include <utility>

namespace ld {

// The scratch allocator gets fresh pages from the system and then unmaps them
// all at the end of the allocator object's lifetime.
template <class Memory>
using ScratchAllocator =
    trivial_allocator::BasicOwningAllocator<trivial_allocator::PageAllocator<Memory>>;

// The initial-exec allocator gets fresh pages from the system.  When they've
// been written, they'll be made read-only.  They're never freed.  Both the
// current whole-page chunk and the previous one allocated are kept writable.
// This always permits doing two consecutive allocations of data structures and
// then updating the first data structure to point to the second.
template <class Memory>
using InitialExecAllocatorBase =
    trivial_allocator::BasicLeakyAllocator<trivial_allocator::SealedPageAllocator<Memory, 1>>;

template <class Memory>
class InitialExecAllocator : public InitialExecAllocatorBase<Memory> {
 public:
  using Base = InitialExecAllocatorBase<Memory>;

  using Base::Base;

  // On destruction, seal the outstanding pages.
  ~InitialExecAllocator() { std::move(this->allocate_function()).Seal(); }
};

}  // namespace ld
