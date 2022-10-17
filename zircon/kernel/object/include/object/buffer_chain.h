// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_BUFFER_CHAIN_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_BUFFER_CHAIN_H_

#include <lib/page_cache.h>
#include <lib/user_copy/user_ptr.h>
#include <stdint.h>
#include <string.h>
#include <zircon/types.h>

#include <cstddef>
#include <new>

#include <fbl/algorithm.h>
#include <fbl/canary.h>
#include <fbl/intrusive_single_list.h>
#include <ktl/algorithm.h>
#include <ktl/move.h>
#include <vm/page.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

// BufferChain is a list of buffers allocated from the PMM.
//
// It's designed for use with channel messages.  Pages backing a BufferChain are marked as
// vm_page_state::IPC.
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
// BufferChain does not dynamically allocate. An initial Alloc() call allocates a list of
// pages that will be used to build the BufferChain. This allocation can exceed the size
// actually used by the buffer chain and the excess buffers can be freed with a call to
// FreeUnusedBuffers(). The motivation for sometimes allocating more than needed and later
// freeing is that the number of needed buffers is sometimes initially unknown and it is
// presumed to be more efficient to do a single allocation than multiple allocations.
//
// BufferChain uses a private PageCache to improve performance under load by
// avoiding contention on the PMM. The page cache is tunable by the kernel
// command line parameter kernel.bufferchain.reserve-pages.
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
  constexpr static size_t kSizeOfBufferChain =
      sizeof(BufferList) + 2 * sizeof(list_node) + sizeof(size_t) + sizeof(BufferList::iterator);

  // kContig is the number of bytes guaranteed to be stored contiguously in any buffer
  constexpr static size_t kContig = kRawDataSize - kSizeOfBufferChain;

  // Copies |size| bytes from this chain starting at offset |src_offset| to |dst|.
  //
  // |src_offset| must be in the range [0, kContig).
  zx_status_t CopyOut(user_out_ptr<char> dst, size_t src_offset, size_t size) {
    DEBUG_ASSERT(src_offset < buffers_.front().size());
    size_t copy_offset = src_offset;
    size_t rem = size;
    const auto end = buffers_.end();
    for (auto iter = buffers_.begin(); rem > 0 && iter != end; ++iter) {
      const size_t copy_len = ktl::min(rem, iter->size() - copy_offset);
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
    zx::result<page_cache::PageCache::AllocateResult> unused_pages_result =
        page_cache_.Allocate(num_buffers);
    if (unused_pages_result.is_error()) {
      return nullptr;
    }

    // We now have a list of pages.  Allocate a page for the initial Buffer and construct the
    // BufferChain within it.
    BufferChain::BufferList temp;
    list_node used_pages = LIST_INITIAL_VALUE(used_pages);
    zx_status_t status =
        AddNextBuffer(&unused_pages_result->page_list, &used_pages, &temp, temp.end());
    if (unlikely(status != ZX_OK)) {
      return nullptr;
    }
    BufferChain* chain =
        new (temp.front().data()) BufferChain(&temp, &unused_pages_result->page_list, &used_pages);

    return chain;
  }

  // Frees |chain| and its buffers.
  static void Free(BufferChain* chain) {
    // Remove the buffers and vm_page_t's from the chain *before* destroying it.
    BufferChain::BufferList buffers(ktl::move(*chain->buffers()));
    list_node pages = LIST_INITIAL_VALUE(pages);
    list_move(&chain->used_pages_, &pages);
    list_splice_after(&chain->unused_pages_, &pages);

    chain->~BufferChain();

    while (!buffers.is_empty()) {
      BufferChain::Buffer* buf = buffers.pop_front();
      buf->Buffer::~Buffer();
    }
    page_cache_.Free(ktl::move(pages));
  }

  // Free unused pages.
  void FreeUnusedBuffers() { page_cache_.Free(ktl::move(unused_pages_)); }

  // Skips the specified number of bytes, so they won't be consumed by Append or AppendKernel.
  //
  // Assumes that it is called only at the beginning of the buffer chain.
  void Skip(size_t size) {
    DEBUG_ASSERT(buffer_offset_ == 0);
    DEBUG_ASSERT(size <= kContig);
    buffer_offset_ += size;
  }

  // Appends |size| bytes from |src| to this chain|.
  //
  // If there is insufficient remaining space in the buffer chain, ZX_ERR_OUT_OF_RANGE will be
  // returned.
  zx_status_t Append(user_in_ptr<const char> src, size_t size) { return AppendCommon(src, size); }

  // Same as Append except |src| can be in kernel space.
  //
  // If there is insufficient remaining space in the buffer chain, ZX_ERR_OUT_OF_RANGE will be
  // returned.
  zx_status_t AppendKernel(const char* src, size_t size);

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

  static void InitializePageCache(uint32_t level);

 private:
  explicit BufferChain(BufferList* buffers, list_node* unused_pages, list_node* used_pages) {
    buffer_tail_ = buffers->begin();
    buffers_.swap(*buffers);

    buffer_offset_ = 0;

    list_move(unused_pages, &unused_pages_);
    list_move(used_pages, &used_pages_);

    // |this| now lives inside the first buffer.
    buffers_.front().set_reserved(sizeof(BufferChain));
  }

  ~BufferChain() {
    DEBUG_ASSERT(list_is_empty(&used_pages_));
    DEBUG_ASSERT(list_is_empty(&unused_pages_));
  }

  // Add a new Buffer to the end of |buffers| (marked by |buffer_tail|) by taking a page from the
  // |unused_pages| list. In doing so, this page will be moved to the |used_pages| list.
  static zx_status_t AddNextBuffer(list_node* unused_pages, list_node* used_pages,
                                   BufferList* buffers, BufferList::iterator buffer_tail) {
    if (list_is_empty(unused_pages)) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    vm_page_t* page = list_remove_head_type(unused_pages, vm_page, queue_node);
    DEBUG_ASSERT(page != nullptr);
    DEBUG_ASSERT(page->state() == vm_page_state::ALLOC);
    page->set_state(vm_page_state::IPC);
    list_add_tail(used_pages, &page->queue_node);

    void* va = paddr_to_physmap(page->paddr());
    if (buffers->is_empty()) {
      buffers->push_front(new (va) BufferChain::Buffer);
    } else {
      buffers->insert_after(buffer_tail, new (va) BufferChain::Buffer);
    }
    return ZX_OK;
  }

  // |PTR_IN| is a user_in_ptr-like type.
  template <typename PTR_IN>
  zx_status_t AppendCommon(PTR_IN src, size_t size) {
    if (size == 0) {
      return ZX_OK;
    }
    if (buffer_tail_ == buffers_.end()) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    size_t rem = size;
    while (true) {
      Buffer& buf = *buffer_tail_;
      const size_t copy_len = ktl::min(rem, buf.size() - buffer_offset_);
      char* dst = buf.data() + buffer_offset_;
      zx_status_t status = src.copy_array_from_user(dst, copy_len);
      if (unlikely(status != ZX_OK)) {
        buffer_tail_ = buffers_.end();
        return status;
      }

      buffer_offset_ += copy_len;
      src = src.byte_offset(copy_len);
      rem -= copy_len;

      if (rem == 0) {
        break;
      }
      status = AddNextBuffer(&unused_pages_, &used_pages_, &buffers_, buffer_tail_);
      if (status != ZX_OK) {
        buffer_tail_ = buffers_.end();
        return status;
      }
      buffer_offset_ = 0;
      buffer_tail_++;
    }
    return ZX_OK;
  }

  // Take care when adding fields as BufferChain lives inside the first buffer of buffers_.
  BufferList buffers_;

  // Iterator pointing to the last valid element in the buffer.
  BufferList::iterator buffer_tail_;

  // Position of the next byte to write in the Buffer pointed to by |buffer_tail_|.
  size_t buffer_offset_;

  // used_pages_ is a list of vm_page_t descriptors for the pages that back BufferList.
  list_node used_pages_ = LIST_INITIAL_VALUE(used_pages_);

  // unused_pages is a list of vm_page_t descriptors for pages that have been allocated but have
  // not yet been used. These pages will be migrated to used_pages_ once they are needed for
  // the BufferList.
  list_node unused_pages_ = LIST_INITIAL_VALUE(unused_pages_);

  inline static page_cache::PageCache page_cache_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(BufferChain);
};
static_assert(sizeof(BufferChain) == BufferChain::kSizeOfBufferChain, "");

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_BUFFER_CHAIN_H_
