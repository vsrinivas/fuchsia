// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>

#include <io-scheduler/io-scheduler.h>
#include <io-scheduler/worker.h>

namespace ioscheduler {

zx_status_t Worker::Create(Scheduler* sched, uint32_t id, std::unique_ptr<Worker>* out) {
  fbl::AllocChecker ac;
  std::unique_ptr<Worker> worker(new (&ac) Worker(sched, id));
  if (!ac.check()) {
    fprintf(stderr, "Failed to allocate worker.\n");
    return ZX_ERR_NO_MEMORY;
  }
  if (thrd_create_with_name(&worker->thread_, worker->ThreadEntry, worker.get(), "io-worker") !=
      thrd_success) {
    fprintf(stderr, "Failed to create worker thread.\n");
    return ZX_ERR_NO_MEMORY;
  }
  worker->thread_started_ = true;
  *out = std::move(worker);
  return ZX_OK;
}

Worker::Worker(Scheduler* sched, uint32_t id) : sched_(sched), id_(id) {}

Worker::~Worker() {
  if (thread_started_) {
    thrd_join(thread_, nullptr);
  }
}

int Worker::ThreadEntry(void* arg) {
  Worker* w = static_cast<Worker*>(arg);
  w->WorkerLoop();
  return 0;
}

void Worker::DoAcquire() {
  ZX_DEBUG_ASSERT(!input_closed_);
  SchedulerClient* client = sched_->client();
  const size_t max_ops = 10;
  zx_status_t status;
  size_t acquire_count = 0;
  StreamOp* op_list[max_ops];
  status = client->Acquire(op_list, max_ops, &acquire_count, true);
  if (status == ZX_ERR_CANCELED) {
    // No more ops to read. Drain the streams and exit.
    input_closed_ = true;
    return;
  }
  if (status != ZX_OK) {
    fprintf(stderr, "ioworker %u: Unexpected return status from Acquire() %d\n", id_, status);
    client->Fatal();
    input_closed_ = true;
    return;
  }

  // Containerize all ops for safety.
  UniqueOp uop_list[max_ops];
  for (size_t i = 0; i < acquire_count; i++) {
    uop_list[i].set(op_list[i]);
  }

  // Enqueue ops in the scheduler's priority queue.
  size_t num_error = 0;
  sched_->Enqueue(uop_list, acquire_count, uop_list, &num_error);
  // Any ops remaining in the list have encountered an error and should be released.
  for (size_t i = 0; i < num_error; i++) {
    client->Release(uop_list[i].release());
  }
}

void Worker::ExecuteLoop() {
  ZX_DEBUG_ASSERT(!cancelled_);
  SchedulerClient* client = sched_->client();
  zx_status_t status;

  for (;;) {
    // Fetch an op.
    UniqueOp op;
    status = sched_->Dequeue(&op, false);
    if (status == ZX_ERR_SHOULD_WAIT) {
      // No more ops in scheduler, acquire more.
      break;
    } else if (status == ZX_ERR_CANCELED) {
      // Shutdown initiated.
      cancelled_ = true;
      break;
    }
    ZX_DEBUG_ASSERT(status == ZX_OK);

    Stream* stream;
    if (op->is_deferred()) {
      // Op completion has been deferred. Release it now.
      stream = op->stream();
      stream->ReleaseOp(std::move(op), client);
      continue;
    }

    // Execute it.
    status = client->Issue(op.get());
    if (status == ZX_ERR_ASYNC) {
      // Op queued for async completion. Released when completed.
      // Op is retained in stream.
      op.release();
      continue;
    }
    if (status != ZX_OK) {
      fprintf(stderr, "ioworker %u: Unexpected return status from Issue() %d\n", id_, status);
      // Mark op as failed.
      op->set_result(ZX_ERR_IO);
    }
    stream = op->stream();
    stream->ReleaseOp(std::move(op), client);
  }
}

void Worker::WorkerLoop() {
  while ((!input_closed_) || (!cancelled_)) {
    // Fetch ops from the client.
    if (!input_closed_) {
      DoAcquire();
    }
    // Drain the priority queue.
    if (!cancelled_) {
      ExecuteLoop();
    }
  }
}

}  // namespace ioscheduler
