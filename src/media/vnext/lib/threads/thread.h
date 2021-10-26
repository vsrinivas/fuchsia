// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_THREADS_THREAD_H_
#define SRC_MEDIA_VNEXT_LIB_THREADS_THREAD_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

namespace fmlib {

// This class wraps an |async::Loop| and an |async::Executor|, providing lifetime management for
// shared threads.
//
// A |Thread| is really a shared pointer to state. As such, it can be moved or copied freely.
// Moving is preferred, because no reference count changes are required. Passing by reference is
// preferred when a |Thread| is borrowed from the caller. When the last |Thread| for a given
// instance of shared state is destroyed, the state is destroyed and resources created when the
// original |Thread| was constructed are cleaned up.
class Thread {
 public:
  // Starts a new thread with the given name and returns a shared pointer to a |Thread| that
  // represents the new thread.
  static Thread CreateNewThread(const char* thread_name);

  // Returns a shared pointer to a |Thread| representing the thread represented by |loop|.
  static Thread CreateForLoop(async::Loop& loop);

  ~Thread() = default;

  // Determines whether the current thread is the one represented by this |Thread|.
  bool is_current() const { return dispatcher() == async_get_default_dispatcher(); }

  // Returns the |async::Loop| for this |Thread|.
  async::Loop& loop() {
    FX_CHECK(shared_);
    return *shared_->loop_;
  }

  // Returns the |async::Loop| for this |Thread|.
  const async::Loop& loop() const {
    FX_CHECK(shared_);
    return *shared_->loop_;
  }

  // Returns the |async::Executor| for this |Thread|.
  async::Executor& executor() {
    FX_CHECK(shared_);
    return shared_->executor_;
  }

  // Returns a pointer to the |async_dispatcher_t| for this |Thread|.
  async_dispatcher_t* dispatcher() const { return loop().dispatcher(); }

  // Posts a closure to this |Thread|'s dispatcher.
  void PostTask(fit::closure handler) { async::PostTask(dispatcher(), std::move(handler)); }

  // Posts a closure to this |Thread|'s dispatcher to be executed at the specified time.
  void PostTaskForTime(fit::closure handler, zx::time time) {
    async::PostTaskForTime(dispatcher(), std::move(handler), time);
  }

  // Posts a closure to this |Thread|'s dispatcher to be executed after the specified interval.
  void PostDelayedTask(fit::closure handler, zx::duration delay) {
    async::PostDelayedTask(dispatcher(), std::move(handler), delay);
  }

  // Schedules a |fpromise::pending_task| for execution.
  void schedule_task(fpromise::pending_task task) { executor().schedule_task(std::move(task)); }

  // Makes a promise that completes after the specified interval.
  fpromise::promise<> MakeDelayedPromise(zx::duration duration) {
    return executor().MakeDelayedPromise(duration);
  }

  // Makes a promise that completes after at the specified time.
  fpromise::promise<> MakePromiseForTime(zx::time deadline) {
    return executor().MakePromiseForTime(deadline);
  }

  // Makes a promise that completes when a handle is signalled.
  fpromise::promise<zx_packet_signal_t, zx_status_t> MakePromiseWaitHandle(
      zx::unowned_handle object, zx_signals_t trigger, uint32_t options = 0) {
    return executor().MakePromiseWaitHandle(std::move(object), trigger, options);
  }

 private:
  struct Shared {
    explicit Shared(const char* thread_name);
    explicit Shared(async::Loop& loop);

    ~Shared();

    async::Loop* loop_;
    async::Loop owned_loop_;
    async::Executor executor_;
  };

  explicit Thread(std::shared_ptr<Shared> state) : shared_(std::move(state)) {}

  std::shared_ptr<Shared> shared_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_THREADS_THREAD_H_
