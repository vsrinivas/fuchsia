// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SHUTDOWN_SHUTDOWN_MANAGER_H_
#define SRC_UI_SCENIC_LIB_SHUTDOWN_SHUTDOWN_MANAGER_H_

#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/fit/promise.h>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace scenic_impl {

// Framework for managing shutdown activities.  All subsystems that require graceful shutdown
// register callbacks that are invoked when |Shutdown()| is called.  These callbacks return a
// promise that is completed when that subsystem is finished shutting down; ShutdownManager waits
// for all of these promises before invoking the |quit_callback| passed to the constructor.
//
// NOTE: this is only for shutdown activities that *must* happen asynchronously on a loop.  It is
// preferable to cleanly using only destructors, if possible.
class ShutdownManager {
 public:
  // |dispatcher| is used for all async operations.  |quit_callback| is invoked after all registered
  // clients have finished shutting down.  If shutdown cannot be completed before the specified
  // timeout, |timeout_callback| is invoked instead, from a thread not associated with |dispatcher|.
  ShutdownManager(
      async_dispatcher_t* dispatcher, fit::closure quit_callback,
      fit::closure timeout_callback = [] { std::terminate(); });
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

  fxl::WeakPtr<ShutdownManager> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  enum class State { kInit, kShuttingDown, kFinishedShuttingDown };
  State state_ = State::kInit;

  async::Executor executor_;
  fit::closure quit_callback_;
  fit::closure timeout_callback_;
  std::vector<ClientCallback> clients_;

  // Used to guarantee that only one of |quit_callback_| and |timeout_callback_| can be invoked.
  std::shared_ptr<std::atomic_bool> shared_bool_;

  fxl::WeakPtrFactory<ShutdownManager> weak_factory_{this};  // must be last
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SHUTDOWN_SHUTDOWN_MANAGER_H_
