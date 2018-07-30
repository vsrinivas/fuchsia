// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <fbl/canary.h>
#include <fbl/intrusive_single_list.h>
#include <lib/user_copy/user_ptr.h>
#include <vm/page.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <zircon/types.h>
#include <zxcpp/new.h>

// BufferChain is a list of fixed-size buffers allocated from the PMM.
//
// It's designed for use with channel messages.  Pages backing a BufferChain are marked as
// VM_PAGE_STATE_IPC.
//
// The BufferChain object itself lives *inside* its first buffer.  Here's what it looks like:
//
//   +--------------------------------+     +--------------------------------+
//   | page                           |     | page                           |
//   |+------------------------------+|     |+------------------------------+|
//   || Buffer                       |+---->|| Buffer                       ||
//   || raw_data:   +---------------+||     || raw_data:    +--------------+||
//   ||             |  BufferChain  |||     ||              | message data |||
//   || reserved -> | ~~~~~~~~~~~~~ |||     || reserved = 0 |  continued   |||
//   ||             |  message data |||     ||              |              |||
//   ||             +---------------+||     ||              +--------------+||
//   |+------------------------------+|     |+------------------------------+|
//   +--------------------------------+     +--------------------------------+
//
class BufferChain {
public:
    class Buffer;
    typedef fbl::SinglyLinkedList<Buffer*> BufferList;

    constexpr static size_t kSizeOfBuffer = PAGE_SIZE;
    constexpr static size_t kSizeOfBufferFields = 16;

    // kRawDataSize is the maximum number of bytes that can fit in a single Buffer.
    //
    // However, not every Buffer in a BufferChain can store this many bytes.  The first Buffer in a
    // BufferChain is special because it also stores the chain itself.  See also kContig.
    constexpr static size_t kRawDataSize = kSizeOfBuffer - kSizeOfBufferFields;

    // Unfortunately, we don't yet know sizeof(BufferChain) so estimate and rely on static_asserts
    // further down to verify.
    constexpr static size_t kSizeOfBufferChain = sizeof(BufferList) + sizeof(list_node);

    // kContig is the number of bytes guaranteed to be stored contiguously in any buffer
    constexpr static size_t kContig = kRawDataSize - kSizeOfBufferChain;

    // Copies |size| bytes from this chain starting at offset |src_offset| to |dst|.
    //
    // |src_offset| must be in the range [0, kContig).
    zx_status_t CopyOut(user_out_ptr<void> dst, size_t src_offset, size_t size) {
        DEBUG_ASSERT(src_offset < buffers_.front().size());
        size_t copy_offset = src_offset;
        size_t rem = size;
        const auto end = buffers_.end();
        for (auto iter = buffers_.begin(); rem > 0 && iter != end; ++iter) {
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

    // Creates a BufferChain with enough buffers to store |size| bytes.
    //
    // It is the caller's responsibility to free the chain with BufferChain::Free.
    //
    // Returns nullptr on error.
    static BufferChain* Alloc(size_t size) {
        size += sizeof(BufferChain);
        const size_t num_buffers = (size + kRawDataSize - 1) / kRawDataSize;

        // Allocate a list of pages.
        list_node pages = LIST_INITIAL_VALUE(pages);
        size_t num_allocated = pmm_alloc_pages(num_buffers, 0, &pages);
        if (unlikely(num_allocated != num_buffers)) {
            pmm_free(&pages);
            return nullptr;
        }

        // Construct a Buffer in each page and add them to a temporary list.
        BufferChain::BufferList temp;
        vm_page_t* page;
        list_for_every_entry (&pages, page, vm_page_t, queue_node) {
            DEBUG_ASSERT(page->state == VM_PAGE_STATE_ALLOC);
            page->state = VM_PAGE_STATE_IPC;
            void* va = paddr_to_physmap(page->paddr());
            temp.push_front(new (va) BufferChain::Buffer);
        }

        // We now have a list of buffers and a list of pages.  Construct a chain inside the first
        // buffer and give the buffers and pages to the chain.
        BufferChain* chain = new (temp.front().data()) BufferChain(&temp, &pages);
        DEBUG_ASSERT(list_is_empty(&pages));

        return chain;
    }

    // Frees |chain| and its buffers.
    static void Free(BufferChain* chain) {
        // Remove the buffers and vm_page_t's from the chain *before* destorying it.
        BufferChain::BufferList buffers(fbl::move(*chain->buffers()));
        list_node pages = LIST_INITIAL_VALUE(pages);
        list_move(&chain->pages_, &pages);

        chain->~BufferChain();

        while (!buffers.is_empty()) {
            BufferChain::Buffer* buf = buffers.pop_front();
            buf->Buffer::~Buffer();
        }
        pmm_free(&pages);
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

        char* data() {
            canary_.Assert();
            return raw_data_ + reserved_;
        }

        size_t size() const { return sizeof(raw_data_) - reserved_; }

        void set_reserved(uint32_t reserved) {
            DEBUG_ASSERT(reserved < sizeof(raw_data_));
            reserved_ = reserved;
        }

    private:
        fbl::Canary<fbl::magic("BUFC")> canary_;
        uint32_t reserved_ = 0;
        char raw_data_[kRawDataSize];
    };
    static_assert(sizeof(BufferChain::Buffer) == BufferChain::kSizeOfBuffer, "");

    BufferList* buffers() { return &buffers_; }

private:
    explicit BufferChain(BufferList* buffers, list_node* pages) {
        buffers_.swap(*buffers);
        list_move(pages, &pages_);

        // |this| now lives inside the first buffer.
        buffers_.front().set_reserved(sizeof(BufferChain));
    }

    ~BufferChain() {
        DEBUG_ASSERT(list_is_empty(&pages_));
    }

    // |PTR_IN| is a user_in_ptr-like type.
    template <typename PTR_IN>
    zx_status_t CopyInCommon(PTR_IN src, size_t dst_offset, size_t size) {
        DEBUG_ASSERT(dst_offset < buffers_.front().size());
        size_t copy_offset = dst_offset;
        size_t rem = size;
        const auto end = buffers_.end();
        for (auto iter = buffers_.begin(); rem > 0 && iter != end; ++iter) {
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

    // pages_ is a list of vm_page_t descriptors for the pages that back BufferList.
    list_node pages_ = LIST_INITIAL_VALUE(pages_);

    DISALLOW_COPY_ASSIGN_AND_MOVE(BufferChain);
};
static_assert(sizeof(BufferChain) == BufferChain::kSizeOfBufferChain, "");
