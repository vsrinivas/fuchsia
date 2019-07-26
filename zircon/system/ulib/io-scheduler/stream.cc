// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <io-scheduler/stream.h>
#include <io-scheduler/io-scheduler.h>

#include <fbl/auto_lock.h>

namespace ioscheduler {

Stream::Stream(uint32_t id, uint32_t pri, Scheduler* sched)
    : id_(id), priority_(pri), sched_(sched) {}

Stream::~Stream() {
  fbl::AutoLock lock(&lock_);
  ZX_DEBUG_ASSERT(open_ == false);
  ZX_DEBUG_ASSERT(in_list_.is_empty());
  ZX_DEBUG_ASSERT(retained_list_.is_empty());
}

zx_status_t Stream::Close() {
  fbl::AutoLock lock(&lock_);
  open_ = false;
  if (retained_list_.is_empty()) {
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
  retained_list_.push_back(op.get());
  bool was_empty = in_list_.is_empty();
  in_list_.push_back(op.release());
  if (was_empty) {
    sched_->SetActive(StreamRef(this));
  }
  return ZX_OK;
}

void Stream::GetNext(UniqueOp* op_out) {
  fbl::AutoLock lock(&lock_);
  UniqueOp op(in_list_.pop_front());
  ZX_DEBUG_ASSERT(op != nullptr);
  *op_out = std::move(op);
  if (!in_list_.is_empty()) {
    sched_->SetActive(StreamRef(this));
  }
}

void Stream::ReleaseOp(UniqueOp op, SchedulerClient* client) {
  StreamOp* sop;
  bool release = false;
  {
    fbl::AutoLock lock(&lock_);
    op->set_stream(nullptr);
    sop = retained_list_.erase(*op.release());
    if (!open_ && retained_list_.is_empty()) {
      // Stream is closed and has no more work to do. Ready for release.
      release = true;
    }
  }
  ZX_DEBUG_ASSERT(sop != nullptr);
  client->Release(sop);
  if (release) {
    sched_->StreamRelease(id_);
  }
}

}  // namespace ioscheduler
