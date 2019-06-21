// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace {

// Trivial bump allocator with a fixed-size heap allocated in .bss.  It leaks
// all freed memory.  This implementation is actually entirely kosher standard
// C++ (though not a kosher implementation of the standard library functions
// because this implementation is not thread-safe and malloc et al must be).
// Since engines cannot start new threads, nobody can tell the difference.
template <size_t HeapSize>
class BumpAllocator {
public:
    void* malloc(size_t n) {
        n = AllocAlign(n);
        if (n < sizeof(heap_) - frontier_) {
            last_block_ = &heap_[frontier_];
            frontier_ += n;
            return last_block_;
        }
        return nullptr;
    }

    void* calloc(size_t n, size_t m) {
        return malloc(n * m);
    }

    void free(void*) {}

    void* realloc(void* ptr, size_t n) {
        if (!ptr) {
            return malloc(n);
        }

        size_t old_frontier = reinterpret_cast<std::byte*>(ptr) - heap_;
        n = AllocAlign(n);
        if (ptr == last_block_) {
            if (n < sizeof(heap_) - old_frontier) {
                frontier_ = old_frontier + n;
                return ptr;
            }
            return nullptr;
        }

        // We don't know how big the old block was, so we might copy too much.
        // But we know the upper bound, so it's safe to copy garbage.
        size_t max_old_size = frontier_ - old_frontier;
        void *new_block = malloc(n);
        if (new_block) {
            memcpy(new_block, ptr, std::min(n, max_old_size));
        }
        return new_block;
    }

private:
    alignas(std::max_align_t) std::byte heap_[HeapSize];
    void* last_block_;
    size_t frontier_;

    static constexpr size_t AllocAlign(size_t n) {
        return ((n + alignof(std::max_align_t) - 1) &
                -alignof(std::max_align_t));
    }
};

// The size of the heap is arbitrary and can be tuned as needed.  Ideally it's
// no larger than is sufficient for the hermetic engine's needs.  But there's
// no real cost to unused heap pages, so the only real need to keep it small
// is to constrain the hermetic engine's peak resource consumption.
BumpAllocator<5 << 20> gHeap;

}

void* malloc(size_t n) { return gHeap.malloc(n); }
void* calloc(size_t n, size_t m) { return gHeap.calloc(n, m); }
void free(void* ptr) { gHeap.free(ptr); }
void* realloc(void* ptr, size_t n) { return gHeap.realloc(ptr, n); }
