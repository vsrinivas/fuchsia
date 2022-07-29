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
// F2fs flushes dirty pages when the number of dirty data pages exceeds a half of
// |kMaxDirtyDataPages|.
constexpr int kMaxDirtyDataPages = 51200;

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
  // It tries to reserve |buffer_| space for |page| subject to writeback. If successful,
  // it unlocks |page| and moves its ownership to |pages_| after copying it to the space,
  // and then PageOperaions will transfers the part of |buffer_| related to |pages_| to disk
  // when a certain condition is met. To allow users to access |page| during writeback,
  // |page| gets unlocked in |pages_| with kWriteback flag set. So, any writers who want to access
  // |page| wait for its writeback by calling Page::WaitOnWriteback(), but readers are free to
  // access to it. If successful, it returns the number of Pages in |pages_|, and a caller
  // must not access |page|.
  zx::status<size_t> ReserveOperation(storage::Operation &operation, LockedPage &page)
      __TA_EXCLUDES(mutex_);
  void ReleaseBuffers(const PageOperations &operation) __TA_EXCLUDES(mutex_);
  bool IsEmpty() __TA_EXCLUDES(mutex_);

 private:
  static constexpr std::string_view kVmoBufferLabels[static_cast<uint32_t>(PageType::kNrPageType)] =
      {"DataSegment", "NodeSegment", "MetaArea"};

  fs::BufferedOperationsBuilder builder_ __TA_GUARDED(mutex_);
  std::vector<fbl::RefPtr<Page>> pages_ __TA_GUARDED(mutex_);
#ifdef __Fuchsia__
  storage::VmoBuffer buffer_ __TA_GUARDED(mutex_);
#else
  storage::ArrayBuffer buffer_ __TA_GUARDED(mutex_);
#endif  // __Fuchsia__
  size_t start_index_ __TA_GUARDED(mutex_) = 0;
  size_t count_ __TA_GUARDED(mutex_) = 0;
  std::condition_variable_any cvar_;
  fs::SharedMutex mutex_;
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
      put_page(std::move(page));
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
  Writer(Bcache *bc);
  Writer() = delete;
  Writer(const Writer &) = delete;
  Writer &operator=(const Writer &) = delete;
  Writer(const Writer &&) = delete;
  Writer &operator=(const Writer &&) = delete;
  ~Writer();

  void ScheduleTask(fpromise::promise<> task);
  void ScheduleWriteback(fpromise::promise<> task);
  // It schedules SubmitPages().
  // If |completion| is set, it notifies the caller of the operation completion.
  void ScheduleSubmitPages(sync_completion_t *completion = nullptr,
                           PageType type = PageType::kNrPageType);
  // It merges Pages to be written.
  void EnqueuePage(storage::Operation &operation, LockedPage &page, PageType type);

 private:
  // It takes writeback operations from |builder_| and passes them to RunReqeusts().
  // When the operations are complete, it groups Pages by the vnode id and passes each group to the
  // regarding FileCache for releasing mappings and committed pages.
  fpromise::promise<> SubmitPages(sync_completion_t *completion, PageType type);

  std::array<std::unique_ptr<SegmentWriteBuffer>, static_cast<uint32_t>(PageType::kNrPageType)>
      write_buffer_;
  fs::TransactionHandler *transaction_handler_ = nullptr;
#ifdef __Fuchsia__
  fpromise::sequencer sequencer_;
  fs::BackgroundExecutor executor_;
  fs::BackgroundExecutor writeback_executor_;
#endif  // __Fuchsia__
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_WRITEBACK_H_
