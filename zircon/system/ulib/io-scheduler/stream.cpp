// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <io-scheduler/stream.h>
#include <io-scheduler/io-scheduler.h>

namespace ioscheduler {

Stream::Stream(uint32_t id, uint32_t pri) : id_(id), priority_(pri) {}

Stream::~Stream() {
    ZX_DEBUG_ASSERT(open_ == false);
    ZX_DEBUG_ASSERT(acquired_list_.is_empty());
}

void Stream::Close() {
    open_ = false;
}

zx_status_t Stream::Push(UniqueOp op, UniqueOp* op_err) {
    ZX_DEBUG_ASSERT(op != nullptr);
    if (!open_) {
        op->set_result(ZX_ERR_BAD_STATE);
        *op_err = std::move(op);
        return ZX_ERR_BAD_STATE;
    }
    acquired_list_.push_back(op.release());
    num_acquired_++;
    return ZX_OK;
}

UniqueOp Stream::Pop() {
    UniqueOp op(acquired_list_.pop_front());
    ZX_DEBUG_ASSERT(op != nullptr);
    num_acquired_--;
    return op;
}

} // namespace ioscheduler