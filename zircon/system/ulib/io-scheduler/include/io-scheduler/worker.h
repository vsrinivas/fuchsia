// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <threads.h>

#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

namespace ioscheduler {

class Scheduler;

class Worker {
public:
    ~Worker();
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Worker);

    // Create a worker object and launch a new thread.
    // |sched| is retained for the lifetime of the worker object.
    static zx_status_t Create(Scheduler* sched, uint32_t id, fbl::unique_ptr<Worker>* out);

private:
    Worker(Scheduler* sched, uint32_t id);

    static int ThreadEntry(void* arg);
    void ThreadMain();
    void WorkerLoop();

    Scheduler* sched_ = nullptr;
    uint32_t id_;
    bool cancelled_ = false;        // Exit has been requested.
    bool thread_started_ = false;
    thrd_t thread_;
};

} // namespace ioscheduler
