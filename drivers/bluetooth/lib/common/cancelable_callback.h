// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <functional>
#include <mutex>

#include <zircon/compiler.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace common {

// CancelableCallback provides a way to run cancelable tasks on any thread.
//
// Each CancelableCallback is obtained from a CancelableCallbackFactory.
// CancelableCallbackFactory::CancelAll() can be used to prevent future
// executions of all previously vended CancelableCallbacks.
//
// CancelableCallbackFactory::CancelAll() blocks if a CancelableCallback is
// running concurrently. This is particularly useful to guarantee the life-time
// of objects that are weakly referenced by a CancelableCallback (and managed
// within the owning scope of the CancelableCallbackFactory).
//
// Once pending tasks on a factory are canceled there is no way to un-cancel
// them. Therefore, a CancelableCallbackFactory is a single-use object; new
// CancelableCallbacks should be obtained from a new CancelableCallbackFactory.
//
// A CancelableCallbackFactory cancels all previously vended CancelableCallbacks
// upon destruction.
//
// EXAMPLE:
//
//   class Foo {
//    public:
//     ...
//     ~Foo() {
//       // If the destructor is non-trivial, then it may be better to cancel
//       // the callbacks explicitly.
//       task_factory_.CancelAll();
//
//       ...clean up other resources...
//     }
//
//     void PostBarTask() {
//       // If |task_factory_| is destroyed before |my_other_thread_runner_|
//       // runs the callback returned from MakeTask(), then the lambda below
//       // will never run.
//       my_other_thread_runner_->PostTask(task_factory_.MakeTask([this] {
//         // If this is run, then |*this| is guaranteed to exist until this
//         // lambda returns (since |task_factory_| is its member). Note that
//         // there is no guarantee that DoBar() itself is thread-safe.
//         DoBar();
//       });
//     }
//
//     void DoBar() {...}
//
//    private:
//     ...
//
//     CancelableCallbackFactory<void()> task_factory_;
//
//     FXL_DISALLOW_COPY_AND_ASSIGN(Foo);
//   };
//
// THREAD-SAFETY:
//
//   A CancelableCallbackFactory should always be accessed on the same thread.
//   CancelableCallbacks can safely exist across threads but should only be
//   modified on one thread.

template <typename Sig>
class CancelableCallback;

template <typename Sig>
class CancelableCallbackFactory;

namespace internal {

// This stores the shared cancelation state for callbacks that are obtained from
// the same factory.
class CancelationState final {
 public:
  CancelationState() : canceled_(false) {}

  // Locks the mutex and holds it until |f| has finished running unless canceled
  // previously.
  template <typename... Args>
  void RunWhileHoldingLock(const std::function<void(Args...)>& f,
                           Args&&... args) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!canceled_)
      f(std::forward<Args>(args)...);
  }

  void Cancel() {
    std::lock_guard<std::mutex> lock(mtx_);
    canceled_ = true;
  }

  bool canceled() const { return canceled_.load(); }

 private:
  std::mutex mtx_;
  std::atomic_bool canceled_;
};

}  // namespace internal

template <typename... Args>
class CancelableCallback<void(Args...)> final {
 public:
  void operator()(Args&&... args) const {
    state_->RunWhileHoldingLock(callback_, std::forward<Args>(args)...);
  }

 private:
  friend class CancelableCallbackFactory<void(Args...)>;

  CancelableCallback(const std::function<void(Args...)>& callback,
                     std::shared_ptr<internal::CancelationState> state)
      : callback_(callback), state_(state) {
    FXL_DCHECK(state_);
    FXL_DCHECK(callback_);
  }

  std::function<void(Args...)> callback_;
  std::shared_ptr<internal::CancelationState> state_;
};

template <typename... Args>
class CancelableCallbackFactory<void(Args...)> final {
 public:
  CancelableCallbackFactory()
      : state_(std::make_shared<internal::CancelationState>()) {
    FXL_DCHECK(state_);
  }

  ~CancelableCallbackFactory() { CancelAll(); }

  CancelableCallback<void(Args...)> MakeTask(
      const std::function<void(Args...)>& f) const {
    FXL_DCHECK(state_);
    FXL_DCHECK(!canceled());
    return CancelableCallback<void(Args...)>(f, state_);
  }

  void CancelAll() const {
    FXL_DCHECK(state_);
    state_->Cancel();
  }

  bool canceled() const { return state_->canceled(); }

 private:
  std::shared_ptr<internal::CancelationState> state_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(CancelableCallbackFactory);
};

}  // namespace common
}  // namespace bluetooth
