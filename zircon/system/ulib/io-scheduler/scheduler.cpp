// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <io-scheduler/io-scheduler.h>

namespace ioscheduler {

zx_status_t Scheduler::Init(SchedulerClient* client, uint32_t options) {
    client_ = client;
    options_ = options;
    return ZX_OK;
}

void Scheduler::Shutdown() { }

zx_status_t Scheduler::StreamOpen(uint32_t id, uint32_t priority) {
    if (priority > kMaxPriority) {
        return ZX_ERR_INVALID_ARGS;
    }

    StreamRef stream;
    zx_status_t status = FindStreamForId(id, &stream);
    if (status != ZX_ERR_NOT_FOUND) {
        return ZX_ERR_ALREADY_EXISTS;
    }

    fbl::AllocChecker ac;
    stream = fbl::AdoptRef(new (&ac) Stream(id, priority));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    streams_.push_back(std::move(stream));
    return ZX_OK;
}

zx_status_t Scheduler::StreamClose(uint32_t id) {
    return RemoveStreamForId(id);
}

zx_status_t Scheduler::Serve() {
    return ZX_OK;
}

void Scheduler::AsyncComplete(SchedulerOp* sop) {

}

Scheduler::~Scheduler() {
    Shutdown();
}

zx_status_t Scheduler::GetStreamForId(uint32_t id, StreamRef* out, bool remove) {
    for (auto iter = streams_.begin(); iter.IsValid(); ++iter) {
        if (iter->Id() == id) {
            if (out) {
                *out = iter.CopyPointer();
            }
            if (remove) {
                streams_.erase(iter);
            }
            return ZX_OK;
        }
    }
    return ZX_ERR_NOT_FOUND;
}

} // namespace ioscheduler
