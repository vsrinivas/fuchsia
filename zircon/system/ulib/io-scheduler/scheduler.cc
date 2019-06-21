// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <io-scheduler/io-scheduler.h>

#include <fbl/auto_lock.h>

namespace ioscheduler {

zx_status_t Scheduler::Init(SchedulerClient* client, uint32_t options) {
    client_ = client;
    options_ = options;
    fbl::AutoLock lock(&stream_lock_);
    shutdown_initiated_ = false;
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
        fbl::AutoLock lock(&stream_lock_);
        shutdown_initiated_ = true;

        // Close all streams.
        for (auto& stream : stream_map_) {
            stream.Close();
        }

        // Wake all workers blocking on the queue. They will observe shutdown_initiated_ and exit.
        active_available_.Broadcast();
    }

    // Block until all worker threads exit.
    workers_.reset();

    {
        // All workers are done.
        fbl::AutoLock lock(&stream_lock_);
        ZX_DEBUG_ASSERT(active_list_.is_empty());
        // Delete any existing stream in the case where no worker threads were launched.
        stream_map_.clear();
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
    if (stream->IsEmpty()) {
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
    ZX_DEBUG_ASSERT(false); // Not yet implemented.
}

Scheduler::~Scheduler() {
    Shutdown();
    ZX_DEBUG_ASSERT(num_streams_ == 0);
    ZX_DEBUG_ASSERT(active_streams_ == 0);
}

zx_status_t Scheduler::Enqueue(UniqueOp* in_list, size_t in_count,
                               UniqueOp* out_list, size_t* out_actual, size_t* out_num_ready) {
    fbl::AutoLock lock(&stream_lock_);
    StreamRef stream = nullptr;
    size_t out_num = 0;
    for (size_t i = 0; i < in_count; i++) {
        UniqueOp op = std::move(in_list[i]);

        // Initialize op fields modified by scheduler.
        op->set_result(ZX_OK);

        // Find stream if not already cached.
        if ((stream == nullptr) || (stream->Id() != op->stream())) {
            stream.reset();
            if (FindStreamLocked(op->stream(), &stream) != ZX_OK) {
                // No stream, mark as failed and leave in list for caller to clean up.
                op->set_result(ZX_ERR_INVALID_ARGS);
                out_list[out_num++] = std::move(op);
                continue;
            }
        }
        bool was_empty = stream->IsEmpty();
        zx_status_t status = stream->Push(std::move(op), &out_list[out_num]);
        if (status != ZX_OK) {
            // Stream is closed, cannot add ops. Op has been added to out_list[out_num]
            out_num++;
            continue;
        }
        if (was_empty) {
            // Add to active list.
            active_list_.push_back(stream);
            active_streams_++;
        }
        acquired_ops_++;
    }
    *out_actual = out_num;
    if (out_num_ready) {
        *out_num_ready = acquired_ops_;
    }
    if (acquired_ops_ > 0) {
        active_available_.Broadcast();  // Wake all worker threads waiting for more work.
    }
    return ZX_OK;
}

zx_status_t Scheduler::Dequeue(UniqueOp* op_out, bool wait) {
    fbl::AutoLock lock(&stream_lock_);
    while (acquired_ops_ == 0) {
        ZX_DEBUG_ASSERT(active_list_.is_empty());
        if (shutdown_initiated_) {
            return ZX_ERR_CANCELED;
        }
        if (!wait) {
            return ZX_ERR_SHOULD_WAIT;
        }
        active_available_.Wait(&stream_lock_);
    }
    ZX_DEBUG_ASSERT(!active_list_.is_empty());
    StreamRef stream = active_list_.pop_front();
    ZX_DEBUG_ASSERT(stream != nullptr);

    *op_out = stream->Pop();
    acquired_ops_--;
    if (stream->IsEmpty()) {
        // Stream has been removed from active list.
        active_streams_--;
    } else {
        // Return stream to tail of active list.
        active_list_.push_back(std::move(stream));
    }
    return ZX_OK;
}

zx_status_t Scheduler::FindStreamLocked(uint32_t id, StreamRef* out) {
    auto iter = stream_map_.find(id);
    if (!iter.IsValid()) {
        return ZX_ERR_NOT_FOUND;
    }
    *out = iter.CopyPointer();
    return ZX_OK;
}

} // namespace ioscheduler
