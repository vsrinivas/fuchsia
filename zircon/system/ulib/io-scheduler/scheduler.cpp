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

    // Wake threads blocking on incoming ops.
    // Threads will complete outstanding work and exit.
    client_->CancelAcquire();

    {
        // Close all streams.
        fbl::AutoLock lock(&stream_lock_);
        for (auto& stream : stream_map_) {
            stream.Close();
        }
    }

    // Block until all worker threads exit.
    workers_.reset();

    {
        // All workers are done.
        fbl::AutoLock lock(&stream_lock_);
        ZX_DEBUG_ASSERT(active_list_.is_empty());
        // Delete any existing stream in the case where no worker threads were launched.
        stream_map_.clear();
        ZX_DEBUG_ASSERT(stream_map_.is_empty());
        num_streams_ = 0;
    }

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
    ZX_DEBUG_ASSERT(client_ != nullptr);

    // Create a single thread for now.
    const uint32_t num_workers = 1;

    for (uint32_t i = 0; i < num_workers; i++) {
        fbl::unique_ptr<Worker> worker;
        zx_status_t status = Worker::Create(this, i, &worker);
        if (status != ZX_OK) {
            fprintf(stderr, "Scheduler: Failed to create worker thread\n");
            Shutdown();
            return status;
        }
        workers_.push_back(std::move(worker));
    }
    return ZX_OK;
}

void Scheduler::AsyncComplete(StreamOp* sop) {

}

Scheduler::~Scheduler() {
    Shutdown();
    ZX_DEBUG_ASSERT(num_streams_ == 0);
    ZX_DEBUG_ASSERT(active_streams_ == 0);
}

} // namespace ioscheduler
