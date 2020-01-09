// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <io-scheduler/io-scheduler.h>
#include <io-scheduler/stream.h>

namespace ioscheduler {

Stream::Stream(uint32_t id, uint32_t pri, Scheduler* sched)
    : id_(id), priority_(pri), sched_(sched) {}

Stream::~Stream() {
  fbl::AutoLock lock(&lock_);
  ZX_DEBUG_ASSERT(open_ == false);
  ZX_DEBUG_ASSERT(ready_ops_.is_empty());
  ZX_DEBUG_ASSERT(issued_ops_.is_empty());
}

bool Stream::IsEmptyLocked() { return ready_ops_.is_empty() && issued_ops_.is_empty(); }

zx_status_t Stream::Close() {
  fbl::AutoLock lock(&lock_);
  open_ = false;
  if (IsEmptyLocked()) {
    return ZX_OK;  // Stream is ready for immediate deletion.
  }
  return ZX_ERR_SHOULD_WAIT;
}

zx_status_t Stream::Insert(UniqueOp op, UniqueOp* op_err) {
  ZX_DEBUG_ASSERT(op != nullptr);
  fbl::AutoLock lock(&lock_);
  if (!open_) {
    op->set_result(ZX_ERR_BAD_STATE);
    *op_err = std::move(op);
    return ZX_ERR_BAD_STATE;
  }
  op->set_stream(this);
  bool was_empty = ready_ops_.is_empty();
  ready_ops_.push_back(op.release());
  if (was_empty && (sched_ != nullptr)) {
    sched_->SetActive(StreamRef(this));
  }
  return ZX_OK;
}

void Stream::GetNext(UniqueOp* op_out) {
  fbl::AutoLock lock(&lock_);
  UniqueOp op(ready_ops_.pop_front());
  ZX_DEBUG_ASSERT(op != nullptr);
  issued_ops_.push_back(op.get());  // Add to issued list.
  *op_out = std::move(op);
  if ((!ready_ops_.is_empty()) && (sched_ != nullptr)) {
    sched_->SetActive(StreamRef(this));
  }
}

void Stream::ReleaseOp(UniqueOp op, SchedulerClient* client) {
  StreamOp* sop;
  bool release = false;
  {
    fbl::AutoLock lock(&lock_);
    op->set_stream(nullptr);
    sop = issued_ops_.erase(*op.release());
    if (!open_ && IsEmptyLocked()) {
      // Stream is closed and has no more work to do. Ready for release.
      release = true;
    }
  }
  ZX_DEBUG_ASSERT(sop != nullptr);
  client->Release(sop);
  if (release && (sched_ != nullptr)) {
    sched_->StreamRelease(id_);
  }
}

}  // namespace ioscheduler
