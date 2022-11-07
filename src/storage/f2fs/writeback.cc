// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

Writer::Writer(Bcache *bc, size_t capacity) : transaction_handler_(bc) {
  write_buffer_ = std::make_unique<StorageBuffer>(bc, capacity, kBlockSize, "WriteBuffer");
}

Writer::~Writer() {
  sync_completion_t completion;
  ScheduleSubmitPages(&completion);
  ZX_ASSERT(sync_completion_wait(&completion, ZX_TIME_INFINITE) == ZX_OK);
}

zx::result<> Writer::EnqueuePages() {
  std::lock_guard lock(mutex_);
  while (!pages_.is_empty()) {
    auto page = pages_.pop_front();
    if (auto num_pages_or = write_buffer_->ReserveWriteOperation(std::move(page));
        num_pages_or.is_error()) {
      if (num_pages_or.status_value() == ZX_ERR_UNAVAILABLE) {
        // No available buffers. Need to submit pending StorageOperations to free buffers.
        pages_.push_front(std::move(page));
        return zx::ok();
      }
      // If |page| has an invalid addr, just drop it.

    } else if (num_pages_or.value() >= kDefaultBlocksPerSegment) {
      // Merged enough StorageOperations. Submit it.
      return zx::ok();
    }
  }
  // No pending Pages.
  return zx::error(ZX_ERR_STOP);
}

fpromise::promise<> Writer::SubmitPages(sync_completion_t *completion) {
  return fpromise::make_promise([this, completion]() mutable {
    while (true) {
      auto submit_or = EnqueuePages();
      zx_status_t ret = ZX_OK;
      // No need to release vmo buffers of |operations| in the same order they are reserved in
      // StorageBuffer.
      auto operations = write_buffer_->TakeWriteOperations();
      if (!operations.IsEmpty()) {
        if (ret = transaction_handler_->RunRequests(operations.TakeOperations()); ret != ZX_OK) {
          FX_LOGS(WARNING) << "[f2fs] Write IO error. " << ret;
        }
        operations.Completion(ret, [ret](fbl::RefPtr<Page> page) {
          if (ret != ZX_OK && page->IsUptodate()) {
            if (page->GetVnode().IsMeta() || ret == ZX_ERR_UNAVAILABLE) {
              // When it fails to write metadata or the block device is not available,
              // set kCpErrorFlag to enter read-only mode.
              page->GetVnode().fs()->GetSuperblockInfo().SetCpFlags(CpFlag::kCpErrorFlag);
            } else {
              // When IO errors occur with node and data Pages, just set a dirty flag
              // to retry it with another LBA.
              LockedPage locked_page(page);
              locked_page->SetDirty();
            }
          }
          page->ClearColdData();
          page->ClearWriteback();
          return ret;
        });
      }
      if (submit_or.is_error()) {
        break;
      }
    }
    if (completion) {
      sync_completion_signal(completion);
    }
    return fpromise::ok();
  });
}

void Writer::ScheduleTask(fpromise::promise<> task) {
#ifdef __Fuchsia__
  executor_.schedule_task(sequencer_.wrap(std::move(task)));
#else   // __Fuchsia__
  [[maybe_unused]] auto result = fpromise::run_single_threaded(std::move(task));
  assert(result.is_ok());
#endif  // __Fuchsia__
}

void Writer::ScheduleWriteback(fpromise::promise<> task) {
#ifdef __Fuchsia__
  writeback_executor_.schedule_task(std::move(task));
#else   // __Fuchsia__
  [[maybe_unused]] auto result = fpromise::run_single_threaded(std::move(task));
#endif  // __Fuchsia__
}

void Writer::ScheduleSubmitPages(sync_completion_t *completion, PageList pages) {
  if (!pages.is_empty()) {
    std::lock_guard lock(mutex_);
    pages_.splice(pages_.end(), pages);
  }
  auto task = SubmitPages(completion);
  ScheduleTask(std::move(task));
}

Reader::Reader(Bcache *bc, size_t capacity) : transaction_handler_(bc) {
  constexpr uint32_t kDefaultAllocationUnit = 128;
  buffer_ = std::make_unique<StorageBuffer>(bc, capacity, kBlockSize, "ReadBuffer",
                                            kDefaultAllocationUnit);
}

zx::result<std::vector<LockedPage>> Reader::SubmitPages(std::vector<LockedPage> pages,
                                                        std::vector<block_t> addrs) {
  auto operation_or = buffer_->ReserveReadOperations(pages, std::move(addrs));
  if (operation_or.is_error()) {
    // If every Page in |pages| targets to either kNullAddr or kNewAddr, it returns
    // |ZX_ERR_CANCELED| as no IO is required. In this case, return zx::ok() with |pages|.
    if (operation_or.status_value() != ZX_ERR_CANCELED) {
      return operation_or.take_error();
    }
  } else {
    ZX_DEBUG_ASSERT(!operation_or.value().IsEmpty());
    zx_status_t ret;
    ret = transaction_handler_->RunRequests(operation_or.value().TakeOperations());
    operation_or.value().Completion(ret, [ret](const fbl::RefPtr<Page> &page) {
      if (ret == ZX_OK) {
        page->SetUptodate();
      }
      return ZX_OK;
    });
    if (ret != ZX_OK) {
      FX_LOGS(WARNING) << "[f2fs] Read IO error. " << ret;
      return zx::error(ret);
    }
  }
  return zx::ok(std::move(pages));
}

}  // namespace f2fs
