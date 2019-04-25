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
    for ( ; ; ) {
        size_t actual_count = 0;
        StreamOp* op_list[max_ops];
        status = client->Acquire(op_list, max_ops, &actual_count, true);
        if (status == ZX_ERR_CANCELED) {
            // Cancel received, no more ops to read. Drain the streams and exit.
            break;
        }
        if (status != ZX_OK) {
            fprintf(stderr, "Unexpected return status from Acquire() %d\n", status);
            client->Fatal();
            break;
        }
        // Dummy issue loop. In the future, ops will be added to the scheduler.
        for (size_t i = 0; i < actual_count; i++) {
            UniqueOp ref(op_list[i]);
            status = client->Issue(ref.get());
            ZX_DEBUG_ASSERT(status == ZX_OK);   // Require synchronous completion, for now.
            client->Release(ref.release());
        }
    }
}

} // namespace ioscheduler
