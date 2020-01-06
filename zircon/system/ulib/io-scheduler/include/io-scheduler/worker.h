// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IO_SCHEDULER_WORKER_H_
#define IO_SCHEDULER_WORKER_H_

#include <stdint.h>
#include <threads.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/macros.h>

namespace ioscheduler {

class Scheduler;

class Worker {
 public:
  ~Worker();
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Worker);

  // Create a worker object and launch a new thread.
  // |sched| is retained for the lifetime of the worker object.
  static zx_status_t Create(Scheduler* sched, uint32_t id, std::unique_ptr<Worker>* out);

 private:
  Worker(Scheduler* sched, uint32_t id);

  static int ThreadEntry(void* arg);
  void WorkerLoop();   // Main worker loop.
  void DoAcquire();    // Acquire new ops.
  void ExecuteLoop();  // Issue available ops.

  Scheduler* sched_ = nullptr;
  uint32_t id_;
  bool cancelled_ = false;     // Exit has been requested.
  bool input_closed_ = false;  // The op source has been closed.
  bool thread_started_ = false;
  thrd_t thread_;
};

}  // namespace ioscheduler

#endif  // IO_SCHEDULER_WORKER_H_
