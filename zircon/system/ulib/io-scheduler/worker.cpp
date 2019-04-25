// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <io-scheduler/worker.h>

#include <stdio.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <io-scheduler/io-scheduler.h>

namespace ioscheduler {

zx_status_t Worker::Create(Scheduler* sched, uint32_t id, fbl::unique_ptr<Worker>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<Worker> worker(new (&ac) Worker(sched, id));
    if (!ac.check()) {
        fprintf(stderr, "Failed to allocate worker.\n");
        return ZX_ERR_NO_MEMORY;
    }
    if (thrd_create(&worker->thread_, worker->ThreadEntry, worker.get()) != ZX_OK) {
        fprintf(stderr, "Failed to create worker thread.\n");
        return ZX_ERR_NO_MEMORY;
    }
    worker->thread_started_ = true;
    *out = std::move(worker);
    return ZX_OK;
}

Worker::Worker(Scheduler* sched, uint32_t id) : sched_(sched), id_(id) { }

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

void Worker::WorkerLoop() {
    const size_t max_ops = 10;
    SchedulerClient* client = sched_->client();
    zx_status_t status;
    while (!cancelled_) {

        // Fetch ops from the client.

        size_t acquire_count = 0;
        StreamOp* op_list[max_ops];
        status = client->Acquire(op_list, max_ops, &acquire_count, true);
        if (status == ZX_ERR_CANCELED) {
            // Cancel received, no more ops to read. Drain the streams and exit.
            break;
        }
        if (status != ZX_OK) {
            fprintf(stderr, "Unexpected return status from Acquire() %d\n", status);
            client->Fatal();
            break;
        }

        // Containerize all ops for safety.
        UniqueOp uop_list[max_ops];
        for (size_t i = 0; i < acquire_count; i++) {
            uop_list[i].set(op_list[i]);
        }

        // Enqueue ops in the scheduler's priority queue.

        size_t num_ready = 0;
        size_t num_error = 0;
        sched_->Enqueue(uop_list, acquire_count, uop_list, &num_error, &num_ready);
        // Any ops remaining in the list have encountered an error and should be released.
        for (size_t i = 0; i < num_error; i++) {
            client->Release(uop_list[i].release());
        }

        // Drain the priority queue.

        for ( ; ; ) {

            // Fetch an op.

            UniqueOp op;
            status = sched_->Dequeue(&op, false);
            if (status == ZX_ERR_SHOULD_WAIT) {
                // No more ops.
                break;
            } else if (status == ZX_ERR_CANCELED) {
                // Shutdown initiated.
                cancelled_ = true;
                break;
            } else if (status != ZX_OK) {
                fprintf(stderr, "Dequeue() failed %d\n", status);
                return;
            }

            // Execute it.

            status = client->Issue(op.get());
            if (status == ZX_OK) {
                // Op completed successfully or encountered a synchronous error.
                client->Release(op.release());
            } else if (status == ZX_ERR_ASYNC) {
                // Op queued for async completion. Released when completed.

                // Todo: transfer op to pending list to await async completion.
                // sched_->AddAsync(std::move(op));
                ZX_DEBUG_ASSERT(false);
            } else {
                fprintf(stderr, "Unexpected return status from Issue() %d\n", status);
                // Mark op as failed.
                op->set_result(ZX_ERR_IO);
                client->Release(op.release());
            }
        }
    }
}

} // namespace ioscheduler
