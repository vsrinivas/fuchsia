// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEDIA_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CLOSURE_QUEUE_H_
#define GARNET_LIB_MEDIA_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CLOSURE_QUEUE_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/synchronization/thread_annotations.h>

#include <memory>
#include <mutex>
#include <queue>
#include <threads.h>

class ClosureQueue {
 public:
  // This can be called on any thread.  Do not call SetDispatcher() after this
  // constructor.
  ClosureQueue(async_dispatcher_t* dispatcher, thrd_t dispatcher_thread);
  // Must call SetDispatcher() before using the queue.
  ClosureQueue();

  void SetDispatcher(async_dispatcher_t* dispatcher, thrd_t dispatcher_thread);

  // This must be called only on dispatcher_thread.
  ~ClosureQueue();

  // If StopAndClear() isn't called yet, runs to_run on dispatcher.
  // If StopAndClear() has already been called, deletes to_run on this thread.
  // If StopAndClear() is called after Enqueue() returns but before to_run has
  // been run on dispatcher, deletes to_run on thread that calls StopAndClear().
  //
  // If run, to_run will run using the dispatcher, on the dispatcher_thread.
  //
  // This can be called on any thread.
  //
  // TODO(dustingreen): Consider adding an over-full threshold that permits
  // client code to notice when the queue is more full than expected (more full
  // than makes any sense for a given protocol).
  void Enqueue(fit::closure to_run);

  // This can only be called on the dispatcher_thread.  This prevents any
  // additional calls to Enqueue() from actually enqueing anything, and deletes
  // any previously-queued tasks that haven't already run.
  //
  // This is idempotent, and will run automatically at the start of
  // ~ClosureQueue, but client code is encouraged to call StopAndClear() earlier
  // than ~ClosureQueue if that helps ensure that all the captures of all the
  // queued lambdas will still be fully usable up to the point where
  // StopAndClear() is called.
  //
  // This must be called only on dispatcher_thread.
  void StopAndClear();
  bool is_stopped();

 private:
  class Impl {
   public:
    static std::shared_ptr<Impl> Create(async_dispatcher_t* dispatcher, thrd_t dispatcher_thread);
    ~Impl();
    void Enqueue(std::shared_ptr<Impl> self_shared, fit::closure to_run);
    void StopAndClear();
    bool is_stopped();

   private:
    Impl(async_dispatcher_t* dispatcher, thrd_t dispatcher_thread);
    void TryRunAll();
    std::mutex lock_;
    // Starts non-nullptr.  Set to nullptr to indicate that StopAndClear() has
    // run.
    async_dispatcher_t* dispatcher_ FXL_GUARDED_BY(lock_){};
    const thrd_t dispatcher_thread_{};
    std::queue<fit::closure> pending_ FXL_GUARDED_BY(lock_);
  };

  std::shared_ptr<Impl> impl_;
};

#endif  // GARNET_LIB_MEDIA_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CLOSURE_QUEUE_H_
