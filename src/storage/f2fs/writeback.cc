// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

Writer::~Writer() {
  {
    sync_completion_t completion;
    ScheduleSubmitPages(&completion);
    ZX_ASSERT(sync_completion_wait(&completion, ZX_TIME_INFINITE) == ZX_OK);
  }
}

void Writer::EnqueuePage(storage::Operation operation, fbl::RefPtr<f2fs::Page> page) {
  bool issue = false;
  {
    std::lock_guard lock(mutex_);
    builder_.Add(operation, page.get());
    pages_.push_back(std::move(page));
    if (pages_.size() >= kDefaultBlocksPerSegment) {
      issue = true;
    }
  }
  if (issue) {
    ScheduleSubmitPages(nullptr);
  }
}

std::vector<storage::BufferedOperation> Writer::TakePagesUnsafe() {
  return builder_.TakeOperations();
}

fpromise::promise<> Writer::SubmitPages(sync_completion_t *completion) {
  std::vector<storage::BufferedOperation> operations;
  std::vector<fbl::RefPtr<f2fs::Page>> pages;
  {
    std::lock_guard lock(mutex_);
    operations = TakePagesUnsafe();
    pages = std::move(pages_);
  }
  ZX_ASSERT(pages.size() == operations.size());
  if (operations.empty()) {
    if (completion) {
      return fpromise::make_promise([completion]() { sync_completion_signal(completion); });
    }
    return fpromise::make_ok_promise();
  }
  return fpromise::make_promise(
      [this, completion, operations = std::move(operations), pages = std::move(pages)]() mutable {
        bool redirty = false;
        if (zx_status_t ret = transaction_handler_->RunRequests(operations); ret != ZX_OK) {
          // Redirty all Pages.
          FX_LOGS(WARNING) << "[f2fs] RunRequest fails..Redirty Pages..";
          redirty = true;
        }
        for (block_t nio = 0; nio < pages.size(); ++nio) {
          auto page = std::move(pages[nio]);
          pages[nio] = nullptr;
          if (redirty && page->IsUptodate()) {
            page->SetDirty();
          }
          page->ClearWriteback();
          Page::PutPage(std::move(page), false);
        }
        if (fs_->GetSuperblockInfo().GetPageCount(CountType::kWriteback) >=
            static_cast<int>(kDefaultBlocksPerSegment)) {
          FX_LOGS(WARNING) << "[f2fs] High pending WB Pages : "
                           << fs_->GetSuperblockInfo().GetPageCount(CountType::kWriteback);
        }
        if (completion) {
          sync_completion_signal(completion);
        }
      });
}

void Writer::ScheduleTask(fpromise::pending_task task) {
  return executor_.schedule_task(std::move(task));
}

void Writer::ScheduleSubmitPages(sync_completion_t *completion) {
  return ScheduleTask(SubmitPages(completion));
}

}  // namespace f2fs
