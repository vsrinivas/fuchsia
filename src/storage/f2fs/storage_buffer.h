// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_STORAGE_BUFFER_H_
#define SRC_STORAGE_F2FS_STORAGE_BUFFER_H_

#ifndef __Fuchsia__
#include <storage/buffer/array_buffer.h>
#endif  // __Fuchsia__

namespace f2fs {

class VmoBufferKey : public fbl::DoublyLinkedListable<std::unique_ptr<VmoBufferKey>> {
 public:
  VmoBufferKey(const uint64_t offset, const vmoid_t vmoid) : vmo_offset_(offset), vmo_id_(vmoid) {}
  VmoBufferKey() = delete;
  VmoBufferKey(const VmoBufferKey &) = delete;
  VmoBufferKey &operator=(const VmoBufferKey &) = delete;
  VmoBufferKey(const VmoBufferKey &&) = delete;
  VmoBufferKey &operator=(const VmoBufferKey &&) = delete;
  uint64_t GetKey() const { return vmo_offset_; }
  vmoid_t GetVmoId() const { return vmo_id_; }

 private:
  const uint64_t vmo_offset_;
  const vmoid_t vmo_id_;
};

class PageOperations;
using VmoKeyList = fbl::DoublyLinkedList<std::unique_ptr<VmoBufferKey>>;
// StorageBuffer implements an allocator for pre-allocated vmo buffers attached to a VmoidRegistry
// object. When there are available buffers in the free list, allocation operations are O(1). If the
// free list is empty, a caller waits for buffers. Free operations are O(1) as well.
class StorageBuffer {
 public:
  StorageBuffer(Bcache *bc, size_t blocks, uint32_t block_size, std::string_view label);
  StorageBuffer() = delete;
  StorageBuffer(const StorageBuffer &) = delete;
  StorageBuffer &operator=(const StorageBuffer &) = delete;
  StorageBuffer(const StorageBuffer &&) = delete;
  StorageBuffer &operator=(const StorageBuffer &&) = delete;
  ~StorageBuffer();

  // It tries to reserve |buffer_| space for |page| subject to writeback. If successful,
  // it unlocks |page| and moves its ownership to |pages_| after copying it to the space,
  // and then PageOperations will transfers the part of |buffer_| related to |pages_| to disk
  // when a certain condition is met. To allow users to access |page| during writeback,
  // |page| gets unlocked in |pages_| with kWriteback flag set. So, any writers who want to access
  // |page| wait for its writeback by calling Page::WaitOnWriteback(), but readers are free to
  // access to it. If successful, it returns the number of Pages in |pages_|, and a caller
  // must not access |page|.
  zx::status<size_t> ReserveWriteOperation(fbl::RefPtr<Page> page, block_t blk_addr)
      __TA_EXCLUDES(mutex_);
  void ReleaseWriteBuffers(const PageOperations &operation, const zx_status_t io_status)
      __TA_EXCLUDES(mutex_);
  bool IsEmpty() __TA_EXCLUDES(mutex_);
  PageOperations TakeWriteOperations() __TA_EXCLUDES(mutex_);

 private:
  void Init() __TA_EXCLUDES(mutex_);
#ifdef __Fuchsia__
  storage::VmoBuffer buffer_ __TA_GUARDED(mutex_);
#else
  storage::ArrayBuffer buffer_ __TA_GUARDED(mutex_);
#endif
  fs::BufferedOperationsBuilder builder_ __TA_GUARDED(mutex_);
  std::vector<fbl::RefPtr<Page>> pages_ __TA_GUARDED(mutex_);
  VmoKeyList free_list_ __TA_GUARDED(mutex_);
  VmoKeyList inflight_list_ __TA_GUARDED(mutex_);
  std::condition_variable_any cvar_;
  fs::SharedMutex mutex_;
};

using PageOperationCallback = fit::function<void(const PageOperations &, const zx_status_t)>;
// A utility class, holding a collection of write requests with data buffers of
// StorageBuffer, ready to be transmitted to persistent storage.
class PageOperations {
 public:
  PageOperations() = delete;
  PageOperations(std::vector<storage::BufferedOperation> operations,
                 std::vector<fbl::RefPtr<Page>> pages, PageOperationCallback release_buffers)
      : operations_(std::move(operations)),
        pages_(std::move(pages)),
        release_buffers_(std::move(release_buffers)) {}
  PageOperations(std::vector<storage::BufferedOperation> operations,
                 std::vector<fbl::RefPtr<Page>> pages, VmoKeyList list,
                 PageOperationCallback release_buffers)
      : operations_(std::move(operations)),
        pages_(std::move(pages)),
        release_buffers_(std::move(release_buffers)),
        list_(std::move(list)) {}
  PageOperations(const PageOperations &operations) = delete;
  PageOperations &operator=(const PageOperations &) = delete;
  PageOperations(const PageOperations &&op) = delete;
  PageOperations &operator=(const PageOperations &&) = delete;
  PageOperations(PageOperations &&op) = default;
  PageOperations &operator=(PageOperations &&) = default;
  ~PageOperations() {
    ZX_DEBUG_ASSERT(pages_.empty());
    ZX_DEBUG_ASSERT(operations_.empty());
    ZX_DEBUG_ASSERT(list_.is_empty());
  }

  std::vector<storage::BufferedOperation> TakeOperations() { return std::move(operations_); }
  // When the IOs for PageOperations complete, Reader or Writer calls it to release storage buffers
  // and handle the IO completion with |pages_| according to |io_status|.
  void Completion(zx_status_t io_status, PageCallback put_page) {
    release_buffers_(*this, io_status);
    for (auto &page : pages_) {
      put_page(std::move(page));
    }
    pages_.clear();
    pages_.shrink_to_fit();
  }
  bool IsEmpty() const { return pages_.empty(); }
  size_t GetLength() const { return pages_.size(); }
  VmoKeyList TakeVmoKeys() const { return std::move(list_); }

 private:
  std::vector<storage::BufferedOperation> operations_;
  std::vector<fbl::RefPtr<Page>> pages_;
  PageOperationCallback release_buffers_;
  mutable VmoKeyList list_;
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_STORAGE_BUFFER_H_
