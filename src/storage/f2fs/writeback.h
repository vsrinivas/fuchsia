// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_WRITEBACK_H_
#define SRC_STORAGE_F2FS_WRITEBACK_H_

#include "src/lib/storage/vfs/cpp/journal/background_executor.h"

namespace f2fs {

class F2fs;

class Writer {
 public:
  Writer(F2fs *fs, fs::TransactionHandler *handler) : fs_(fs), transaction_handler_(handler) {}
  Writer() = delete;
  Writer(const Writer &) = delete;
  Writer &operator=(const Writer &) = delete;
  Writer(const Writer &&) = delete;
  Writer &operator=(const Writer &&) = delete;
  ~Writer();

  void ScheduleTask(fpromise::pending_task task);
  // It schedules SubmitPages().
  // If |completion| is set, it notifies the caller of the operation completion.
  void ScheduleSubmitPages(sync_completion_t *completion) __TA_EXCLUDES(mutex_);
  // It merges Pages to be written.
  void EnqueuePage(storage::Operation operation, fbl::RefPtr<f2fs::Page> page)
      __TA_EXCLUDES(mutex_);
  std::vector<storage::BufferedOperation> TakePagesUnsafe() __TA_REQUIRES(mutex_);

 private:
  // It takes writeback operations from |builder_| and passes them to RunReqeusts().
  // When the operations are complete, it groups Pages by the vnode id and passes each group to the
  // regarding FileCache for releasing mappings and committed pages.
  fpromise::promise<> SubmitPages(sync_completion_t *completion);

  std::vector<fbl::RefPtr<f2fs::Page>> pages_ __TA_GUARDED(mutex_);
  F2fs *fs_ = nullptr;
  fs::TransactionHandler *transaction_handler_ = nullptr;
  std::mutex mutex_;
  // TODO: Maintain a separate builder for each stream.
  fs::BufferedOperationsBuilder builder_ __TA_GUARDED(mutex_);
  fs::BackgroundExecutor executor_;
  // TODO: Track the last block for each PageType segment
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_WRITEBACK_H_
