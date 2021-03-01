// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_allocator.h"

#include <zircon/assert.h>

#include <atomic>

namespace sysmem_driver {

MemoryAllocator::MemoryAllocator(llcpp::fuchsia::sysmem2::wire::HeapProperties properties)
    : heap_properties_(std::move(properties)) {
  static std::atomic_uint64_t id;
  id_ = id++;
}

MemoryAllocator::~MemoryAllocator() {
  for (auto& it : destroy_callbacks_) {
    it.second();
  }
}

void MemoryAllocator::set_ready() { ZX_PANIC("not implemented"); }

bool MemoryAllocator::is_ready() { return true; }

void MemoryAllocator::AddDestroyCallback(intptr_t key, fit::callback<void()> callback) {
  ZX_DEBUG_ASSERT(destroy_callbacks_.find(key) == destroy_callbacks_.end());
  destroy_callbacks_[key] = std::move(callback);
}

void MemoryAllocator::RemoveDestroyCallback(intptr_t key) {
  // The key isn't required to be in the map in case of failures during
  // create.  Erase if present.
  destroy_callbacks_.erase(key);
}

}  // namespace sysmem_driver
