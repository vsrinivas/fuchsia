// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <io-scheduler/io-scheduler.h>

#include <fbl/auto_lock.h>

namespace ioscheduler {

zx_status_t Scheduler::Init(SchedulerClient* client, uint32_t options) {
    client_ = client;
    options_ = options;
    return ZX_OK;
}

void Scheduler::Shutdown() {
    if (client_ == nullptr) {
        return; // Not initialized or already shut down.
    }

    // Close all streams.
    fbl::AutoLock lock(&stream_lock_);
    for (auto& stream : stream_map_) {
        stream.Close();
    }

    // TODO: Wait for completion.
    // For now, remove erase the streams until there are worker threads to do so.
    while (stream_map_.pop_front() != nullptr) {};

    client_ = nullptr;
}

zx_status_t Scheduler::StreamOpen(uint32_t id, uint32_t priority) {
    if (priority > kMaxPriority) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AutoLock lock(&stream_lock_);
    auto iter = stream_map_.find(id);
    if (iter.IsValid()) {
        return ZX_ERR_ALREADY_EXISTS;
    }

    fbl::AllocChecker ac;
    StreamRef stream = fbl::AdoptRef(new (&ac) Stream(id, priority));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    stream_map_.insert(std::move(stream));
    num_streams_++;
    return ZX_OK;
}

zx_status_t Scheduler::StreamClose(uint32_t id) {
    fbl::AutoLock lock(&stream_lock_);
    auto iter = stream_map_.find(id);
    if (!iter.IsValid()) {
        return ZX_ERR_INVALID_ARGS;
    }
    StreamRef stream = iter.CopyPointer();
    stream->Close();
    // Once closed, the stream cannot transition from idle to active.
    if (!stream->IsActive()) {
        // Stream is inactive, delete here.
        // Otherwise, it will be deleted by the worker that drains it.
        stream_map_.erase(*stream);
    }

    return ZX_OK;
}

zx_status_t Scheduler::Serve() {
    return ZX_OK;
}

void Scheduler::AsyncComplete(SchedulerOp* sop) {

}

Scheduler::~Scheduler() {
    Shutdown();
    ZX_DEBUG_ASSERT(num_streams_ == 0);
    ZX_DEBUG_ASSERT(active_streams_ == 0);
}

} // namespace ioscheduler
