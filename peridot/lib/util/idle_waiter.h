// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_UTIL_IDLE_WAITER_H_
#define PERIDOT_LIB_UTIL_IDLE_WAITER_H_

#include <vector>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fxl/functional/closure.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <lib/fxl/memory/weak_ptr.h>

namespace util {

// A utility class that exposes the |WaitUntilIdle| function, which invokes a
// callback after all outstanding tasks (not including delayed tasks) have been
// processed on a task queue and all user-instrumented asynchronous activities
// have completed. This function is typically exposed on a service's debug FIDL
// interface and used for test synchronization.
//
// This class is bound to a message loop using SetLoop(). Unbound
// instances of this class may exist in unit tests.
class IdleWaiter final {
 public:
  class Activity : public fxl::RefCountedThreadSafe<Activity> {
   public:
    Activity(fxl::WeakPtr<IdleWaiter> tracker);
    ~Activity();

   private:
    fxl::WeakPtr<IdleWaiter> tracker_;
  };

  using ActivityToken = fxl::RefPtr<Activity>;

  IdleWaiter();
  ~IdleWaiter();

  void SetLoop(async::Loop* loop);
  async::Loop* loop() const { return loop_; }

  // Registers an ongoing activity which prevents this app from being
  // considered idle. When the last copy of |ActivityToken| is destroyed, the
  // activity is considered complete.
  //
  // |ActivityToken| should typically be captured in any lambda triggered while
  // handling a call. It is not necessary to register an activity that completes
  // synchronously.
  //
  // This method must be invoked on the thread that constructed |IdleWaiter|,
  // and |ActivityToken| must be released on the same thread.
  ActivityToken RegisterOngoingActivity();

  // Checking for inactivity involves draining the message loop of ready tasks.
  // Doing so must happen outside of the main message loop, so when the time
  // comes for an idle check, this class escapes the main message loop with a
  // quit task. When this happens, app main should call |FinishIdleCheck| and
  // then resume the main message loop if it returns |true|.
  //
  // TODO(rosswang): Remove this requirement if |RunUntilIdle| is ever supported
  //  from within an outer message loop.
  bool FinishIdleCheck();

  // Waits until the app has reached a steady state such that no further
  // activity will occur unless acted upon from the outside:
  // * when there are no ready tasks in the message loop; delayed tasks are not
  //   included
  // * when no outstanding copies of |ActivityToken|s created by
  //   |RegisterOngoingActivity| are held by the instrumented app
  void WaitUntilIdle(fxl::Closure callback);

 private:
  // Idle checks must be performed outside of the main message loop, so
  // |PostIdleCheck| escapes the main message loop with a quit task. See
  // |FinishIdleCheck| for more information.
  void PostIdleCheck();

  async::Loop* loop_{};
  std::vector<fxl::Closure> callbacks_;

  Activity* activity_ = nullptr;
  bool idle_check_pending_ = false;

  fxl::WeakPtrFactory<IdleWaiter> weak_ptr_factory_;
};

}  // namespace util

#endif  // PERIDOT_LIB_UTIL_IDLE_WAITER_H_
