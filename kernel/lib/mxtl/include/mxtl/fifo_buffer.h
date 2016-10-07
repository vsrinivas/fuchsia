// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <new.h>
#include <pow2.h>

#include <kernel/vm.h>

namespace mxtl {

//  Bounded FIFO buffer class. Not thread-safe.
//
//  Example usage
//
//    class MyThing { ... };
//
//    mxtl::FifoBuffer<MyThing> fifo;
//
//    fifo.Init(256);    allocates enough memory for 256 objects.
//
//    auto athing = fifo.push_tail();   creates a new MyThing and
//                                      pushes it into the FIFO tail.
//
//    athing.foo(...);     after push_tail() the object is already
//                         visible to pop_head() so locking is necessay
//                         for multithreaded uses.
//    ...
//
//    othing = fifo.pop_head();  after this call the memory backing |othing|
//                               is already available for pushing into the
//                               container so again locking is necessary for
//                               concurrent access.
//    othing.bar(...);
//
//    othing->~MyThing();   while possibly unnecessary, because push_tail()
//                          actually calls the constructor somebody should
//                          call the destructor when done.
//
//    ....
//    fifo.clear();          // destroys the remaning objects. The fifo
//                           // destructor also does this.
//
template <typename T>
class FifoBuffer {
public:
    FifoBuffer()
        : log2_(0u), avail_(0u), head_(0u), tail_(0u), buffer_(nullptr) {
    }

    FifoBuffer(const FifoBuffer &) = delete;
    FifoBuffer& operator=(const FifoBuffer &) = delete;

    ~FifoBuffer() {
        clear();
        free_memory();
    }

    // |buffer_count| is the maximum number of T's the container can hold
    // and must be a power of two.
    bool Init(uint32_t buffer_count) {
        clear();
        free_memory();

        log2_ = log2_uint_floor(buffer_count);
        if (valpow2(log2_) != buffer_count)
            return false;

        auto kspace = vmm_get_kernel_aspace();
        void* start = nullptr;
        status_t st = vmm_alloc(kspace, "fifo-buffer", sizeof(T) * buffer_count, &start,
                                PAGE_SIZE_SHIFT, 0, 0,
                                ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
        if (st < 0)
          return false;

        avail_ = buffer_count;
        buffer_ = reinterpret_cast<T*>(start);

        return true;
    }

    T* push_tail() {
        if (!avail_)
          return nullptr;

        auto p = &buffer_[tail_];
        tail_ = modpow2(++tail_, log2_);
        --avail_;
        return new (p) T;
    }

    const T* peek_head() const {
        if (is_empty())
          return nullptr;

        return &buffer_[head_];
    }

    T* pop_head() {
        if (is_empty())
          return nullptr;

        auto t = &buffer_[head_];
        head_ = modpow2(++head_, log2_);
        ++avail_;
        return t;
    }

    bool is_empty() const {
        return (avail_ == valpow2(log2_));
    }

    bool is_full() const {
        return (avail_ == 0u);
    }

    void clear() {
        for (;;) {
            T* t = pop_head();
            if (!t)
                break;
            t->~T();
        }
    }

private:
    void free_memory() {
        log2_ = avail_ = head_ = tail_ = 0u;
        if (!buffer_)
            return;
        auto kspace = vmm_get_kernel_aspace();
        vmm_free_region(kspace, reinterpret_cast<vaddr_t>(buffer_));
        buffer_ = nullptr;
    }

    uint32_t log2_;
    uint32_t avail_;
    uint32_t head_;
    uint32_t tail_;
    T* buffer_;
};


}
