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
    for ( ; ; ) {
        size_t actual_count = 0;
        SchedulerOp* op_list[max_ops];
        zx_status_t status = client->Acquire(op_list, max_ops, &actual_count, true);
        if (status == ZX_ERR_CANCELED) {
            // Cancel received, no more ops to read. Drain the streams and exit.
            break;
        }
        // Temporary: delay since this loop doesn't do any real work yet and has nothing else to
        // wait on. Will be deleted when loop body fills out.
        zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    }
}

} // namespace ioscheduler
