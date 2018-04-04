// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <lib/user_copy/user_ptr.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>
#include <zxcpp/new.h>

class BufferChainFreeList;

// BufferChain is a list of fixed-size buffers allocated from a free list (BufferChainFreeList).
//
// The BufferChain object itself lives *inside* its first buffer.
class BufferChain {
public:
    class Buffer;
    typedef fbl::SinglyLinkedList<Buffer*> BufferList;

    // kRawSize is the number of bytes that can fit in a single buffer.
    //
    // However, not every buffer in a chain can store this many bytes.  The first buffer in a chain
    // is special because it also stores the chain itself.  See also kContig.
    //
    // kRawSize should be small enough to minimize wasted space and large enough to be efficient
    // when copying large buffer chains.
    //
    // TODO(maniscalco): When we move off malloc/free, statically compute this value such that a
    // Buffer *actually* fits in a single page (including all overhead).
    constexpr static size_t kRawSize = PAGE_SIZE;

    // kContig is the number of bytes guaranteed to be stored contiguously in any buffer.
    constexpr static size_t kContig = kRawSize - sizeof(BufferList);

    // Copies |size| bytes from this chain starting at offset |src_offset| to |dst|.
    //
    // |src_offset| must be in the range [0, kContig).
    zx_status_t CopyOut(user_out_ptr<void> dst, size_t src_offset, size_t size) {
        DEBUG_ASSERT(src_offset < buffers_.front().size());
        size_t copy_offset = src_offset;
        size_t rem = size;
        for (auto iter = buffers_.begin(); rem > 0 && iter != buffers_.end(); ++iter) {
            const size_t copy_len = fbl::min(rem, iter->size() - copy_offset);
            const char* src = iter->data() + copy_offset;
            const zx_status_t status = dst.copy_array_to_user(src, copy_len);
            if (unlikely(status != ZX_OK)) {
                return status;
            }
            dst = dst.byte_offset(copy_len);
            rem -= copy_len;
            copy_offset = 0;
        }
        return ZX_OK;
    }

    // Copies |size| bytes from |src| to this chain starting at offset |dst_offset|.
    //
    // |dst_offset| must be in the range [0, kContig).
    zx_status_t CopyIn(user_in_ptr<const void> src, size_t dst_offset, size_t size) {
        return CopyInCommon(src, dst_offset, size);
    }

    // Same as CopyIn except |src| can be in kernel space.
    zx_status_t CopyInKernel(const void* src, size_t dst_offset, size_t size);

    class Buffer final : public fbl::SinglyLinkedListable<Buffer*> {
    public:
        Buffer() = default;
        ~Buffer() = default;

        char* data() { return raw_data_ + reserved_; }

        size_t size() const { return sizeof(raw_data_) - reserved_; }

        void set_reserved(size_t reserved) {
            DEBUG_ASSERT(reserved < sizeof(raw_data_));
            reserved_ = reserved;
        }

    private:
        size_t reserved_ = 0;
        char raw_data_[kRawSize];
    };

    BufferList* buffers() { return &buffers_; }

private:
    friend BufferChainFreeList;
    explicit BufferChain(BufferList* list) {
        buffers_.swap(*list);
        // |this| now lives inside the first buffer.
        buffers_.front().set_reserved(sizeof(BufferChain));
    }
    ~BufferChain() = default;

    // |PTR_IN| is a user_in_ptr-like type.
    template <typename PTR_IN>
    zx_status_t CopyInCommon(PTR_IN src, size_t dst_offset, size_t size) {
        DEBUG_ASSERT(dst_offset < buffers_.front().size());
        size_t copy_offset = dst_offset;
        size_t rem = size;
        for (auto iter = buffers_.begin(); rem > 0 && iter != buffers_.end(); ++iter) {
            const size_t copy_len = fbl::min(rem, iter->size() - copy_offset);
            char* dst = iter->data() + copy_offset;
            const zx_status_t status = src.copy_array_from_user(dst, copy_len);
            if (unlikely(status != ZX_OK)) {
                return status;
            }
            src = src.byte_offset(copy_len);
            rem -= copy_len;
            copy_offset = 0;
        }
        return ZX_OK;
    }

    // Take care when adding fields as BufferChain lives inside the first buffer of buffers_.
    BufferList buffers_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(BufferChain);
};

// The BufferChain must fit within a single buffer.
static_assert(BufferChain::kRawSize - sizeof(BufferChain) == BufferChain::kContig, "");

// BufferChainFreeList is a free list of Buffers.
//
// It's backed by the heap (malloc/free) and populated lazily.
//
// Objects of this class are thread-safe.
class BufferChainFreeList {
public:
    // Constructs a BufferChainFreeList that can grow to at most |max_buffers|.
    //
    // |max_buffers| must be > 0.
    //
    // When the free list is full, freeing an element will return it to the heap.
    constexpr BufferChainFreeList(unsigned max_buffers)
        : max_buffers_(max_buffers), num_buffers_(0) { DEBUG_ASSERT(max_buffers_ > 0); }

    ~BufferChainFreeList();

    // Creates a BufferChain with enough buffers to store |size| bytes.
    //
    // It is the callers responsibility to free the chain with BufferChain::Free.
    //
    // Returns nullptr on error.
    BufferChain* Alloc(size_t size) {
        size += sizeof(BufferChain);
        const size_t num_buffers = (size + BufferChain::kRawSize - 1) / BufferChain::kRawSize;
        BufferChain::BufferList buffers;
        if (unlikely(AllocBuffers(num_buffers, &buffers) != ZX_OK)) {
            return nullptr;
        }

        // We now have a list of buffers.  Construct a chain inside the first buffer and give the
        // buffers to the chain.
        BufferChain* chain = new (buffers.front().data()) BufferChain(&buffers);
        return chain;
    }

    // Frees |chain| and its buffers, returning them to the free list.
    void Free(BufferChain* chain) {
        // Remove the buffers from the chain before destorying it.
        BufferChain::BufferList buffers(fbl::move(*chain->buffers()));
        chain->~BufferChain();
        {
            fbl::AutoLock guard(&mutex_);
            FreeBuffersLocked(&buffers);
        }
    }

private:
    // Allocates |count| buffers and returns them on |result|.
    //
    // If a non-empty |result| is passed in, its elements may be removed and freed.
    //
    // Returns ZX_OK if successful. On error, result is unmodified.
    zx_status_t AllocBuffers(size_t count, BufferChain::BufferList* result) {
        BufferChain::BufferList list;
        {
            fbl::AutoLock guard(&mutex_);
            for (size_t i = 0; i < count; ++i) {
                if (unlikely(free_list_.is_empty())) {
                    // The free list is empty, time to refill it.
                    for (unsigned i = 0; i < max_buffers_; ++i) {
                        BufferChain::Buffer* buf =
                            static_cast<BufferChain::Buffer*>(malloc(sizeof(BufferChain::Buffer)));
                        if (unlikely(buf == nullptr)) {
                            FreeBuffersLocked(&list);
                            return ZX_ERR_NO_MEMORY;
                        }
                        new (buf) BufferChain::Buffer;
                        free_list_.push_front(buf);
                        ++num_buffers_;
                    }
                }
                list.push_front(free_list_.pop_front());
                --num_buffers_;
            }
        }
        list.swap(*result);
        return ZX_OK;
    }

    // Destroys and frees all elements of |list|.
    //
    // Callers must be holding |mutex_|.
    void FreeBuffersLocked(BufferChain::BufferList* list) TA_REQ(&mutex_) {
        while (!list->is_empty()) {
            BufferChain::Buffer* buf = list->pop_front();
            buf->Buffer::~Buffer();
            if (num_buffers_ < max_buffers_) {
                new (buf) BufferChain::Buffer;
                free_list_.push_front(buf);
                ++num_buffers_;
            } else {
                // The free list is full so return this one to the heap.
                free(buf);
            }
        }
    }

    // Protects all fields below.
    fbl::Mutex mutex_;
    const unsigned max_buffers_ TA_GUARDED(&mutex_);
    unsigned num_buffers_ TA_GUARDED(&mutex_);
    BufferChain::BufferList free_list_ TA_GUARDED(&mutex_);

    DISALLOW_COPY_ASSIGN_AND_MOVE(BufferChainFreeList);
};
