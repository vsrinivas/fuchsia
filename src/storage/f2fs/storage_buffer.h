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
  VmoBufferKey(const uint64_t offset) : vmo_offset_(offset) {}
  VmoBufferKey() = delete;
  VmoBufferKey(const VmoBufferKey &) = delete;
  VmoBufferKey &operator=(const VmoBufferKey &) = delete;
  VmoBufferKey(const VmoBufferKey &&) = delete;
  VmoBufferKey &operator=(const VmoBufferKey &&) = delete;
  uint64_t GetKey() const { return vmo_offset_; }

 private:
  const uint64_t vmo_offset_;
};

class PageOperations;
using VmoKeyList = fbl::DoublyLinkedList<std::unique_ptr<VmoBufferKey>>;

// StorageBuffer implements an allocator for pre-allocated vmo buffers attached to a VmoidRegistry
// object. When there are available buffers in the free list, allocation operations are O(1). If the
// free list is empty, a caller waits for buffers. Free operations are O(1) as well.
class StorageBuffer {
 public:
  // StorageBuffer reserves vmo buffers in |allocation_unit|. Therefore, |allocation_unit| should be
  // bigger than the number of pages requested in |ReserveWriteOperation| or |ReserveReadOperations|
  // to get the maximum performance. It should be also smaller than |blocks|, because |blocks| is
  // the total size of vmo buffers.
  StorageBuffer(Bcache *bc, size_t blocks, uint32_t block_size, std::string_view label,
                uint32_t allocation_unit = 1);
  StorageBuffer() = delete;
  StorageBuffer(const StorageBuffer &) = delete;
  StorageBuffer &operator=(const StorageBuffer &) = delete;
  StorageBuffer(const StorageBuffer &&) = delete;
  StorageBuffer &operator=(const StorageBuffer &&) = delete;
  ~StorageBuffer();

  // It tries to reserve |buffer_| for |page| subject to writeback. If successful,
  // it pushes |page| to |io_pages_| after copying its contents to the reserved buffer.
  // To allow readers to access |page| during writeback, it expects that |page| is unlocked
  // with kWriteback flag set before. Any writers who want to access |page| wait for
  // its writeback by calling Page::WaitOnWriteback(), but readers are free to
  // access to it.
  zx::result<size_t> ReserveWriteOperation(fbl::RefPtr<Page> page, block_t blk_addr)
      __TA_EXCLUDES(mutex_);
  // It sorts out which Pages need to transfer to fs::TransactionHandler and tries to reserve
  // |buffer_| for the Pages for read I/Os. If successful, it returns PageOpeartions that
  // convey BufferedOperations and the refptr of the Pages for read I/Os.
  zx::result<PageOperations> ReserveReadOperations(std::vector<LockedPage> &pages,
                                                   const std::vector<block_t> &addrs)
      __TA_EXCLUDES(mutex_);

  void ReleaseReadBuffers(const PageOperations &operation, const zx_status_t io_status)
      __TA_EXCLUDES(mutex_);
  void ReleaseWriteBuffers(const PageOperations &operation, const zx_status_t io_status)
      __TA_EXCLUDES(mutex_);
  // It returns PageOperations that convey BufferedOperations and |pages_| for write I/Os.
  PageOperations TakeWriteOperations() __TA_EXCLUDES(mutex_);

 private:
  void Init() __TA_EXCLUDES(mutex_);
  const uint64_t max_blocks_;
  const uint32_t allocation_unit_;
#ifdef __Fuchsia__
  storage::VmoBuffer buffer_;
#else
  storage::ArrayBuffer buffer_;
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
                 std::vector<fbl::RefPtr<Page>> io_pages, VmoKeyList list,
                 PageOperationCallback io_completion)
      : operations_(std::move(operations)),
        io_pages_(std::move(io_pages)),
        io_completion_(std::move(io_completion)),
        list_(std::move(list)) {}
  PageOperations(const PageOperations &operations) = delete;
  PageOperations &operator=(const PageOperations &) = delete;
  PageOperations(const PageOperations &&op) = delete;
  PageOperations &operator=(const PageOperations &&) = delete;
  PageOperations(PageOperations &&op) = default;
  PageOperations &operator=(PageOperations &&) = default;
  ~PageOperations() {
    ZX_DEBUG_ASSERT(io_pages_.empty());
    ZX_DEBUG_ASSERT(operations_.empty());
    ZX_DEBUG_ASSERT(list_.is_empty());
  }

  std::vector<storage::BufferedOperation> TakeOperations() { return std::move(operations_); }
  zx::result<> PopulatePage(void *data, size_t page_index) const {
    if (page_index >= io_pages_.size()) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    std::memcpy(io_pages_[page_index]->GetAddress(), data, io_pages_[page_index]->BlockSize());
    return zx::ok();
  }
  // When the IOs for PageOperations complete, Reader or Writer calls it to release storage buffers
  // and handle the IO completion with |io_pages_| according to |io_status|.
  void Completion(zx_status_t io_status, PageCallback put_page) {
    io_completion_(*this, io_status);
    for (auto &page : io_pages_) {
      put_page(std::move(page));
    }
    io_pages_.clear();
    io_pages_.shrink_to_fit();
  }
  bool IsEmpty() const { return io_pages_.empty(); }
  VmoKeyList TakeVmoKeys() const { return std::move(list_); }
  size_t GetSize() const { return io_pages_.size(); }

 private:
  std::vector<storage::BufferedOperation> operations_;
  mutable std::vector<fbl::RefPtr<Page>> io_pages_;
  PageOperationCallback io_completion_;
  mutable VmoKeyList list_;
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_STORAGE_BUFFER_H_
