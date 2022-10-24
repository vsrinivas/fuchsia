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
#ifdef __Fuchsia__
  executor_.Terminate();
  writeback_executor_.Terminate();
#endif
}

zx::result<> Writer::EnqueuePage(LockedPage &page, block_t blk_addr, PageType type) {
  ZX_DEBUG_ASSERT(type < PageType::kNrPageType);
  auto ret = write_buffer_->ReserveWriteOperation(page.release(), blk_addr);
  if (ret.is_error()) {
#ifdef __Fuchsia__
    ZX_ASSERT_MSG(false, "Writer failed to reserve buffers. %s", ret.status_string());
#endif
    return ret.take_error();
  } else if (ret.value() >= kDefaultBlocksPerSegment / 2) {
    // Submit Pages once they are merged as much as a half of segment.
    ScheduleSubmitPages(nullptr, type);
  }
  return zx::ok();
}

fpromise::promise<> Writer::SubmitPages(sync_completion_t *completion, PageType type) {
  // We don't need to release vmo buffers of |operations| in the same order they are reserved in
  // StorageBuffer.
  auto operations = write_buffer_->TakeWriteOperations();
  if (!completion && operations.IsEmpty()) {
    return fpromise::make_ok_promise();
  }

  return fpromise::make_promise(
      [this, completion, operations = std::move(operations), type]() mutable {
        zx_status_t ret = ZX_OK;
        if (!operations.IsEmpty()) {
          if (ret = transaction_handler_->RunRequests(operations.TakeOperations()); ret != ZX_OK) {
            FX_LOGS(WARNING) << "[f2fs] Write IO error. " << ret;
          }
          operations.Completion(ret, [ret, type](const fbl::RefPtr<Page> &page) {
            if (ret != ZX_OK && page->IsUptodate()) {
              if (type == PageType::kMeta || type == PageType::kNrPageType ||
                  ret == ZX_ERR_UNAVAILABLE) {
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

void Writer::ScheduleSubmitPages(sync_completion_t *completion, PageType type) {
  auto task = SubmitPages(completion, type);
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
