// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <memory>

#include <fbl/auto_lock.h>
#include <io-scheduler/io-scheduler.h>

namespace ioscheduler {

Scheduler::~Scheduler() {
  Shutdown();
  ZX_DEBUG_ASSERT(all_streams_.is_empty());
  ZX_DEBUG_ASSERT(ready_streams_.is_empty());
  ZX_DEBUG_ASSERT(workers_.is_empty());
}

zx_status_t Scheduler::Init(SchedulerClient* client, uint32_t options) {
  client_ = client;
  options_ = options;
  fbl::AutoLock lock(&lock_);
  shutdown_initiated_ = false;
  return ZX_OK;
}

void Scheduler::Shutdown() {
  if (client_ == nullptr) {
    return;  // Not initialized or already shut down.
  }

  // Wake threads blocking on incoming ops.
  // Threads will complete outstanding work and exit.
  client_->CancelAcquire();
  {
    fbl::AutoLock lock(&lock_);
    shutdown_initiated_ = true;

    // Close all streams.
    for (auto& stream : all_streams_) {
      stream.Close();
    }

    // Wake all workers blocking on the queue. They will observe shutdown_initiated_ and exit.
    ops_available_.Broadcast();
  }

  // Block until all worker threads exit.
  workers_.reset();

  {
    fbl::AutoLock lock(&lock_);
    // Delete any existing stream in the case where no worker threads were launched.
    all_streams_.clear();
  }

  client_ = nullptr;
}

zx_status_t Scheduler::StreamOpen(uint32_t id, uint32_t priority) {
  if (priority > kMaxPriority) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&lock_);
  if (FindLocked(id, nullptr) == ZX_OK) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  fbl::AllocChecker ac;
  StreamRef stream = fbl::AdoptRef(new (&ac) Stream(id, priority));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  all_streams_.insert(std::move(stream));
  return ZX_OK;
}

zx_status_t Scheduler::StreamClose(uint32_t id) {
  fbl::AutoLock lock(&lock_);
  StreamRef stream;
  zx_status_t status = FindLocked(id, &stream);
  if (status != ZX_OK) {
    return status;
  }
  stream->set_flags(kStreamFlagIsClosed);
  if (stream->IsEmpty()) {
    // Stream has no more ops. No more ops can be added since it is now closed.
    // Stream will be deleted when all references are released.
    all_streams_.erase(id);
    return ZX_OK;
  }
  // Stream is closed but still active. No more ops can be added.
  // Stream will be deleted by worker thread that empties it.
  return ZX_OK;
}

zx_status_t Scheduler::Serve() {
  if (client_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  // Create a single thread for now.
  const uint32_t num_workers = 1;

  for (uint32_t i = 0; i < num_workers; i++) {
    std::unique_ptr<Worker> worker;
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
  // TODO(ZX-4741): call async-friendly completion deferral instead of doing it in the
  // caller context.
  ReleaseOp(UniqueOp(sop));
}

zx_status_t Scheduler::InsertOp(UniqueOp op, UniqueOp* op_err) {
  fbl::AutoLock lock(&lock_);
  StreamRef stream;
  zx_status_t status = FindLocked(op->stream_id(), &stream);
  if (status != ZX_OK) {
    op->set_result(ZX_ERR_INVALID_ARGS);
    *op_err = std::move(op);
    return status;
  }
  bool was_ready = stream->HasReady();
  status = stream->Insert(std::move(op), op_err);
  if (status != ZX_OK) {
    // Insertion failed. Op result is set by Insert().
    return status;
  }
  if (!was_ready) {
    ready_streams_.push_back(std::move(stream));
  }
  ops_available_.Signal();
  return ZX_OK;
}

// Enqueue - file a list of ops into their respective streams and schedule those streams.
zx_status_t Scheduler::Enqueue(UniqueOp* in_list, size_t in_count, UniqueOp* out_list,
                               size_t* out_actual) {
  size_t out_num = 0;
  for (size_t i = 0; i < in_count; i++) {
    UniqueOp op = std::move(in_list[i]);
    // Initialize op fields modified by scheduler.
    op->set_result(ZX_OK);
    zx_status_t status = InsertOp(std::move(op), &out_list[out_num]);
    if (status != ZX_OK) {
      // Op was added to out_list with an error result.
      out_num++;
    }
  }
  *out_actual = out_num;
  return ZX_OK;
}

zx_status_t Scheduler::Dequeue(bool wait, UniqueOp* out) {
    fbl::AutoLock lock(&lock_);
  for (;;) {
    StreamRef stream = ready_streams_.pop_front();
    if (stream != nullptr) {
      stream->GetNext(out);
      ZX_DEBUG_ASSERT(*out != nullptr);
      if (stream->HasReady()) {
        // Stream has more ops, return to tail of ready stream queue.
        ready_streams_.push_back(std::move(stream));
      }
      return ZX_OK;
    }

    // No more ops available.
    if (shutdown_initiated_) {
      return ZX_ERR_CANCELED;
    }
    if (!wait) {
      return ZX_ERR_SHOULD_WAIT;
    }
    ops_available_.Wait(&lock_);
  }
}

void Scheduler::ReleaseOp(UniqueOp op) {
  bool stream_done = false;
  uint32_t sid;
  {
    fbl::AutoLock lock(&lock_);
    StreamRef stream;
    sid = op->stream_id();
    zx_status_t status = FindLocked(sid, &stream);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    if (stream == nullptr) {
      fprintf(stderr, "Scheduler: Releasing op with invalid stream id\n");
    } else {
      stream->Complete(op.get());
      stream_done = stream->is_closed() && stream->IsEmpty();
    }
  }

  client_->Release(op.release());

  if (stream_done) {
    fbl::AutoLock lock(&lock_);
    all_streams_.erase(sid);
  }
}

zx_status_t Scheduler::FindLocked(uint32_t id, StreamRef* out) {
  auto iter = all_streams_.find(id);
  if (!iter.IsValid()) {
    return ZX_ERR_NOT_FOUND;
  }
  if (out != nullptr) {
    *out = StreamRef(iter.CopyPointer());
  }
  return ZX_OK;
}

}  // namespace ioscheduler
