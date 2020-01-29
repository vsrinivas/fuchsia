// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <io-scheduler/io-scheduler.h>
#include <io-scheduler/stream.h>

namespace ioscheduler {

Stream::Stream(uint32_t id, uint32_t pri) : id_(id), priority_(pri) {}

Stream::~Stream() {
  ZX_DEBUG_ASSERT(is_closed());
  ZX_DEBUG_ASSERT(ready_ops_.is_empty());
  ZX_DEBUG_ASSERT(issued_ops_.is_empty());
  ZX_DEBUG_ASSERT(deferred_ops_.is_empty());
}

zx_status_t Stream::Close() {
  flags_ |= kStreamFlagIsClosed;
  if (IsEmpty()) {
    return ZX_OK;  // Stream is ready for immediate deletion.
  }
  return ZX_ERR_SHOULD_WAIT;
}

zx_status_t Stream::Insert(UniqueOp op, UniqueOp* op_err) {
  ZX_DEBUG_ASSERT(op != nullptr);
  if (is_closed()) {
    op->set_result(ZX_ERR_BAD_STATE);
    *op_err = std::move(op);
    return ZX_ERR_BAD_STATE;
  }
  ready_ops_.push_back(op.release());
  return ZX_OK;
}

void Stream::GetNext(UniqueOp* op_out) {
  ZX_DEBUG_ASSERT(!IsEmpty());
  ZX_DEBUG_ASSERT(HasReady());
  UniqueOp op(ready_ops_.pop_front());
  ZX_DEBUG_ASSERT(op != nullptr);
  issued_ops_.push_back(op.get());  // Add to issued list.
  *op_out = std::move(op);
}

void Stream::Defer(UniqueOp op) {
  ZX_DEBUG_ASSERT(!IsEmpty());
  ZX_DEBUG_ASSERT(op != nullptr);
  ZX_DEBUG_ASSERT(op->stream_id() == id_);
  op->set_flags(kOpFlagDeferred);
  deferred_ops_.push_back(op.release());
}

void Stream::GetDeferred(UniqueOp* op_out) {
  ZX_DEBUG_ASSERT(!deferred_ops_.is_empty());
  *op_out = UniqueOp(deferred_ops_.pop_front());
}

void Stream::Complete(StreamOp* op) {
  ZX_DEBUG_ASSERT(!IsEmpty());
  ZX_DEBUG_ASSERT(op->stream_id() == id_);
  issued_ops_.erase(*op);
}

}  // namespace ioscheduler
