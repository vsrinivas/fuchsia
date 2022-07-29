// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

#ifdef __Fuchsia__
SegmentWriteBuffer::SegmentWriteBuffer(storage::VmoidRegistry *vmoid_registry, size_t blocks,
                                       uint32_t block_size, PageType type) {
  ZX_DEBUG_ASSERT(type < PageType::kNrPageType);
  ZX_ASSERT(buffer_.Initialize(vmoid_registry, blocks, block_size,
                               kVmoBufferLabels[static_cast<uint32_t>(type)].data()) == ZX_OK);
}
#else  // __Fuchsia__
SegmentWriteBuffer::SegmentWriteBuffer(Bcache *bc, size_t blocks, uint32_t block_size,
                                       PageType type)
    : buffer_(blocks, block_size) {}
#endif

PageOperations SegmentWriteBuffer::TakeOperations() {
  std::lock_guard lock(mutex_);
  return PageOperations(builder_.TakeOperations(), std::move(pages_),
                        [this](const PageOperations &op) { ReleaseBuffers(op); });
}

void SegmentWriteBuffer::ReleaseBuffers(const PageOperations &operation) {
  if (!operation.Empty()) {
    // Decrease count_ to allow waiters to reserve buffer_.
    std::lock_guard lock(mutex_);
    count_ = safemath::CheckSub(count_, operation.GetLength()).ValueOrDie();
    cvar_.notify_all();
  }
}

zx::status<size_t> SegmentWriteBuffer::ReserveOperation(storage::Operation &operation,
                                                        LockedPage &page) {
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
  pages_.push_back(page.release());
  if (++start_index_ == buffer_.capacity()) {
    start_index_ = 0;
  }
  ++count_;
  return zx::ok(pages_.size());
}

bool SegmentWriteBuffer::IsEmpty() {
  fs::SharedLock lock(mutex_);
  return pages_.empty();
}

SegmentWriteBuffer::~SegmentWriteBuffer() { ZX_DEBUG_ASSERT(pages_.size() == 0); }

Writer::Writer(Bcache *bc) : transaction_handler_(bc) {
  for (const auto type : {PageType::kData, PageType::kNode, PageType::kMeta}) {
    write_buffer_[static_cast<uint32_t>(type)] =
        std::make_unique<SegmentWriteBuffer>(bc, kDefaultBlocksPerSegment, kBlockSize, type);
  }
}

Writer::~Writer() {
  sync_completion_t completion;
  ScheduleSubmitPages(&completion);
  ZX_ASSERT(sync_completion_wait(&completion, ZX_TIME_INFINITE) == ZX_OK);
}

void Writer::EnqueuePage(storage::Operation &operation, LockedPage &page, PageType type) {
  ZX_DEBUG_ASSERT(type < PageType::kNrPageType);
  auto ret = write_buffer_[static_cast<uint32_t>(type)]->ReserveOperation(operation, page);
  if (ret.is_error()) {
    // Should not happen.
    ZX_ASSERT(0);
  } else if (ret.value() >= kDefaultBlocksPerSegment / 2) {
    // Submit Pages once they are merged as much as a half of segment.
    ScheduleSubmitPages(nullptr, type);
  }
}

fpromise::promise<> Writer::SubmitPages(sync_completion_t *completion, PageType type) {
  if (!completion && write_buffer_[static_cast<uint32_t>(type)]->IsEmpty()) {
    return fpromise::make_ok_promise();
  }
  return fpromise::make_promise([this, completion, type]() mutable {
    zx_status_t ret = ZX_OK;
    auto operations = write_buffer_[static_cast<uint32_t>(type)]->TakeOperations();
    if (!operations.Empty()) {
      if (ret = transaction_handler_->RunRequests(operations.TakeOperations()); ret != ZX_OK) {
        FX_LOGS(WARNING) << "[f2fs] RunRequest fails with " << ret;
      }
      operations.Completion([ret, type](fbl::RefPtr<Page> page) {
        if (ret != ZX_OK && page->IsUptodate()) {
          if (type == PageType::kMeta || ret == ZX_ERR_UNAVAILABLE) {
            // When it fails to write metadata or the block device is not available,
            // set kCpErrorFlag to enter read-only mode.
            page->GetVnode().Vfs()->GetSuperblockInfo().SetCpFlags(CpFlag::kCpErrorFlag);
          } else {
            // When IO errors occur with node and data Pages, just set a dirty flag
            // to retry it with another LBA.
            LockedPage locked_page(page);
            locked_page->SetDirty();
          }
        }
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
  __UNUSED auto result = fpromise::run_single_threaded(std::move(task));
  assert(result.is_ok());
#endif  // __Fuchsia__
}

void Writer::ScheduleWriteback(fpromise::promise<> task) {
#ifdef __Fuchsia__
  writeback_executor_.schedule_task(std::move(task));
#else   // __Fuchsia__
  __UNUSED auto result = fpromise::run_single_threaded(std::move(task));
#endif  // __Fuchsia__
}

void Writer::ScheduleSubmitPages(sync_completion_t *completion, PageType type) {
  auto task = (type == PageType::kNrPageType)
                  ? SubmitPages(nullptr, PageType::kData)
                        .then([this](fpromise::result<> &result) {
                          return SubmitPages(nullptr, PageType::kNode);
                        })
                        .then([this, completion](fpromise::result<> &result) {
                          return SubmitPages(completion, PageType::kMeta);
                        })
                  : SubmitPages(completion, type);
  ScheduleTask(std::move(task));
}

}  // namespace f2fs
