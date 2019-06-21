// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_allocator.h"

#include <zircon/assert.h>

MemoryAllocator::~MemoryAllocator() {
    for (auto& it : destroy_callbacks_) {
        it.second();
    }
}

void MemoryAllocator::AddDestroyCallback(intptr_t key,
                                         fit::callback<void()> callback) {
    ZX_DEBUG_ASSERT(destroy_callbacks_.find(key) == destroy_callbacks_.end());
    destroy_callbacks_[key] = std::move(callback);
}

void MemoryAllocator::RemoveDestroyCallback(intptr_t key) {
    // The key isn't required to be in the map in case of failures during
    // create.  Erase if present.
    destroy_callbacks_.erase(key);
}
