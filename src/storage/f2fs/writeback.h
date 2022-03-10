// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_WRITEBACK_H_
#define SRC_STORAGE_F2FS_WRITEBACK_H_

#ifdef __Fuchsia__
#include "src/lib/storage/vfs/cpp/journal/background_executor.h"
#else  // __Fuchsia__
#include <lib/fpromise/promise.h>
#include <lib/fpromise/single_threaded_executor.h>

#include <storage/buffer/array_buffer.h>
#endif  // __Fuchsia__

namespace f2fs {

class F2fs;
class PageOperations;
constexpr auto kWriteTimeOut = std::chrono::seconds(60);

class SegmentWriteBuffer {
 public:
#ifdef __Fuchsia__
  SegmentWriteBuffer(storage::VmoidRegistry *vmoid_registry, size_t blocks, uint32_t block_size,
                     PageType type);
#else   // __Fuchsia__
  SegmentWriteBuffer(Bcache *bc, size_t blocks, uint32_t block_size, PageType type);
#endif  // __Fuchsia__
  SegmentWriteBuffer() = delete;
  SegmentWriteBuffer(const SegmentWriteBuffer &) = delete;
  SegmentWriteBuffer &operator=(const SegmentWriteBuffer &) = delete;
  SegmentWriteBuffer(const SegmentWriteBuffer &&) = delete;
  SegmentWriteBuffer &operator=(const SegmentWriteBuffer &&) = delete;
  ~SegmentWriteBuffer();

  PageOperations TakeOperations() __TA_EXCLUDES(mutex_);
  zx::status<uint32_t> ReserveOperation(storage::Operation &operation, fbl::RefPtr<Page> page)
      __TA_EXCLUDES(mutex_);
  void ReleaseBuffers(const PageOperations &operation) __TA_EXCLUDES(mutex_);
  block_t GetCapacity() const { return safemath::checked_cast<block_t>(buffer_.capacity()); }

 private:
  static constexpr std::string_view kVmoBufferLabels[static_cast<uint32_t>(PageType::kNrPageType)] =
      {"DataSegment", "NodeSegment", "MetaArea"};

  fs::BufferedOperationsBuilder builder_ __TA_GUARDED(mutex_);
  std::vector<fbl::RefPtr<Page>> pages_ __TA_GUARDED(mutex_);
#ifdef __Fuchsia__
  storage::VmoBuffer buffer_;
#else
  storage::ArrayBuffer buffer_;
#endif  // __Fuchsia__
  uint32_t start_index_ __TA_GUARDED(mutex_) = 0;
  uint32_t count_ __TA_GUARDED(mutex_) = 0;
  std::condition_variable_any cvar_;
  std::mutex mutex_;
};

using PageOperationCallback = fit::function<void(const PageOperations &)>;
// A utility class, holding a collection of write requests associated with a portion of a single
// SegmentWriteBuffer, ready to be transmitted to persistent storage.
class PageOperations {
 public:
  PageOperations() = delete;
  PageOperations(std::vector<storage::BufferedOperation> operations,
                 std::vector<fbl::RefPtr<Page>> pages, PageOperationCallback release_buffers)
      : operations_(std::move(operations)),
        pages_(std::move(pages)),
        release_buffers_(std::move(release_buffers)) {}
  PageOperations(const PageOperations &operations) = delete;
  PageOperations &operator=(const PageOperations &) = delete;
  PageOperations(const PageOperations &&op) = delete;
  PageOperations &operator=(const PageOperations &&) = delete;
  PageOperations(PageOperations &&op) = default;
  PageOperations &operator=(PageOperations &&) = default;
  ~PageOperations() {
    ZX_DEBUG_ASSERT(pages_.empty());
    ZX_DEBUG_ASSERT(operations_.empty());
  }

  std::vector<storage::BufferedOperation> TakeOperations() { return std::move(operations_); }
  void Completion(PageCallback put_page) {
    release_buffers_(*this);
    for (auto &page : pages_) {
      put_page(page);
    }
    pages_.clear();
    pages_.shrink_to_fit();
  }
  bool Empty() const { return pages_.empty(); }
  size_t GetLength() const { return pages_.size(); }

 private:
  std::vector<storage::BufferedOperation> operations_;
  std::vector<fbl::RefPtr<Page>> pages_;
  PageOperationCallback release_buffers_;
};

class Writer {
 public:
  Writer(F2fs *fs, Bcache *bc);
  Writer() = delete;
  Writer(const Writer &) = delete;
  Writer &operator=(const Writer &) = delete;
  Writer(const Writer &&) = delete;
  Writer &operator=(const Writer &&) = delete;
  ~Writer();

  void ScheduleTask(fpromise::pending_task task);
  // It schedules SubmitPages().
  // If |completion| is set, it notifies the caller of the operation completion.
  void ScheduleSubmitPages(sync_completion_t *completion = nullptr,
                           PageType type = PageType::kNrPageType);
  // It merges Pages to be written.
  void EnqueuePage(storage::Operation &operation, fbl::RefPtr<f2fs::Page> page, PageType type);

 private:
  // It takes writeback operations from |builder_| and passes them to RunReqeusts().
  // When the operations are complete, it groups Pages by the vnode id and passes each group to the
  // regarding FileCache for releasing mappings and committed pages.
  fpromise::promise<> SubmitPages(sync_completion_t *completion, PageType type);

  std::array<std::unique_ptr<SegmentWriteBuffer>, static_cast<uint32_t>(PageType::kNrPageType)>
      write_buffer_;
  F2fs *fs_ = nullptr;
  fs::TransactionHandler *transaction_handler_ = nullptr;
#ifdef __Fuchsia__
  fs::BackgroundExecutor executor_;
#endif  // __Fuchsia__
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_WRITEBACK_H_
