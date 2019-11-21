// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SHUTDOWN_SHUTDOWN_MANAGER_H_
#define SRC_UI_SCENIC_LIB_SHUTDOWN_SHUTDOWN_MANAGER_H_

#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/fit/promise.h>

#include <memory>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace scenic_impl {

// Framework for managing shutdown activities.  All subsystems that require graceful shutdown
// register callbacks that are invoked when |Shutdown()| is called.  These callbacks return a
// promise that is completed when that subsystem is finished shutting down; ShutdownManager waits
// for all of these promises before invoking the |quit_callback| passed to the constructor.
//
// NOTE: this is only for shutdown activities that *must* happen asynchronously on a loop.  It is
// preferable to cleanly using only destructors, if possible.
class ShutdownManager final : public std::enable_shared_from_this<ShutdownManager> {
 public:
  using QuitCallback = fit::closure;
  using TimeoutCallback = fit::function<void(bool)>;

  // |dispatcher| is used for all async operations.  |quit_callback| is invoked after all registered
  // clients have finished shutting down.  If shutdown cannot be completed before the specified
  // timeout, |timeout_callback| is invoked instead, from a thread not associated with |dispatcher|.
  static std::shared_ptr<ShutdownManager> New(
      async_dispatcher_t* dispatcher, QuitCallback quit_callback,
      TimeoutCallback timeout_callback = [](bool timed_out) {
        if (timed_out) {
          std::terminate();
        }
      });



  ~ShutdownManager();

  // Registers a callback that will be invoked when |Shutdown()| is called.  Once |Shutdown()| has
  // been called, it is no longer legal to register additional callbacks.
  using ClientCallback = fit::function<fit::promise<>()>;
  void RegisterClient(ClientCallback client);

  // Attempts to shutdown gracefully.  If the specified |timeout| is exceeded, then the
  // |timeout_callback| will be invoked even though some clients aren't finished shutting down.
  //
  // Only the first call to |Shutdown()| is effective; subsequent calls are ignored.
  void Shutdown(zx::duration timeout);

  // For testing.  NOTE: this callback will be destroyed on a different thread, so be sure not to
  // capture refs to any non-threadsafe objects.
  void set_clock_callback(fit::function<zx::time()> cb);

 private:
  ShutdownManager(async_dispatcher_t* dispatcher, QuitCallback quit_callback,
                  TimeoutCallback timeout_callback);

  enum class State { kInit, kShuttingDown, kFinishedShuttingDown };
  State state_ = State::kInit;

  async::Executor executor_;
  QuitCallback quit_callback_;
  TimeoutCallback timeout_callback_;
  fit::function<zx::time()> clock_callback_ = [] { return zx::time(zx_clock_get_monotonic()); };
  std::vector<ClientCallback> clients_;

  // Used to guarantee that only one of |quit_callback_| and |timeout_callback_| can be invoked.
  std::shared_ptr<std::atomic_bool> shared_bool_;
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SHUTDOWN_SHUTDOWN_MANAGER_H_
