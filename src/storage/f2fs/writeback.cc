// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

SegmentWriteBuffer::SegmentWriteBuffer(storage::VmoidRegistry *vmoid_registry, size_t blocks,
                                       uint32_t block_size) {
  ZX_ASSERT(buffer_.Initialize(vmoid_registry, blocks, block_size, kVmoBufferLabel.data()) ==
            ZX_OK);
}

PageOperations SegmentWriteBuffer::TakeOperations() {
  std::lock_guard lock(mutex_);
  return PageOperations(builder_.TakeOperations(), std::move(pages_),
                        [this](const PageOperations &op) { ReleaseBuffers(op); });
}

void SegmentWriteBuffer::ReleaseBuffers(const PageOperations &operation) {
  if (!operation.Empty()) {
    // Decrease count_ to allow waiters to reserve buffer_.
    std::lock_guard lock(mutex_);
    count_ -= operation.GetLength();
    cvar_.notify_all();
  }
}

zx::status<uint32_t> SegmentWriteBuffer::ReserveOperation(storage::Operation &operation,
                                                          fbl::RefPtr<Page> page) {
  // It will be unmapped when there is no reference.
  ZX_ASSERT(page->Map() == ZX_OK);

  std::lock_guard lock(mutex_);

  // Wait until there is a room in |buffer_|.
  while (count_ == buffer_.capacity()) {
    if (auto wait_result = cvar_.wait_for(mutex_, kWriteTimeOut);
        wait_result == std::cv_status::timeout) {
      return zx::error(ZX_ERR_TIMED_OUT);
    }
  }

  operation.vmo_offset = start_index_;
  // Copy |page| to |buffer| at |start_index_|.
  if (operation.type == storage::OperationType::kWrite) {
    std::memcpy(buffer_.Data(start_index_), page->GetAddress(), page->BlockSize());
  }
  // Here, |operation| can be merged into a previous operation.
  builder_.Add(operation, &buffer_);
  pages_.push_back(std::move(page));
  if (++start_index_ == buffer_.capacity()) {
    start_index_ = 0;
  }
  return zx::ok(++count_);
}

SegmentWriteBuffer::~SegmentWriteBuffer() { ZX_DEBUG_ASSERT(pages_.size() == 0); }

Writer::Writer(F2fs *fs, Bcache *bc) : fs_(fs), transaction_handler_(bc) {
  write_buffer_ =
      std::make_unique<SegmentWriteBuffer>(bc, kDefaultBlocksPerSegment * 2, kBlockSize);
}

Writer::~Writer() {
  sync_completion_t completion;
  ScheduleSubmitPages(&completion);
  ZX_ASSERT(sync_completion_wait(&completion, ZX_TIME_INFINITE) == ZX_OK);
}

void Writer::EnqueuePage(storage::Operation &operation, fbl::RefPtr<f2fs::Page> page) {
  auto ret = write_buffer_->ReserveOperation(operation, std::move(page));
  if (ret.is_error()) {
    // Should not happen.
    ZX_ASSERT(0);
  } else if (ret.value() >= kDefaultBlocksPerSegment) {
    // Submit Pages when they are merged as much as a segment.
    ScheduleSubmitPages(nullptr);
  }
}

fpromise::promise<> Writer::SubmitPages(sync_completion_t *completion) {
  auto operations = write_buffer_->TakeOperations();
  if (operations.Empty()) {
    if (completion) {
      return fpromise::make_promise([completion]() { sync_completion_signal(completion); });
    }
    return fpromise::make_ok_promise();
  }
  return fpromise::make_promise([this, completion, operations = std::move(operations)]() mutable {
    zx_status_t ret = ZX_OK;
    if (ret = transaction_handler_->RunRequests(operations.TakeOperations()); ret != ZX_OK) {
      FX_LOGS(WARNING) << "[f2fs] RunRequest fails..Redirty Pages..";
    }
    operations.Completion([ret](fbl::RefPtr<Page> &page) {
      if (ret != ZX_OK && page->IsUptodate()) {
        // Just redirty it in case of IO failure.
        page->SetDirty();
      }
      page->ClearWriteback();
      Page::PutPage(std::move(page), false);
      return ZX_OK;
    });
    if (fs_->GetSuperblockInfo().GetPageCount(CountType::kWriteback) >
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
