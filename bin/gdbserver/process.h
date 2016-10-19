// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <launchpad/launchpad.h>
#include <magenta/syscalls/exception.h>
#include <magenta/types.h>

#include "lib/ftl/macros.h"
#include "lib/mtl/handles/unique_handle.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

#include "exception_port.h"
#include "thread.h"

namespace debugserver {

class Server;

// Represents an inferior process that the stub is currently attached to.
class Process final {
 public:
  // TODO(armansito): Add a different constructor later for attaching to an
  // already running process.
  explicit Process(Server* server, const std::vector<std::string>& argv);
  ~Process();

  // Creates and initializes the inferior process but does not start it. Returns
  // false if there is an error.
  bool Initialize();

  // Binds an exception port for receiving exceptions from the inferior process.
  // Returns true on success, or false in the case of an error. Initialize MUST
  // be called successfully before calling Attach().
  bool Attach();

  // Detaches an attached process.
  bool Detach();

  // Starts running the process. Returns false in case of an error. Initialize()
  // MUST be called successfully before calling Start().
  bool Start();

  // Returns true if the process has been started via a call to Start();
  bool started() const { return started_; }

  // Returns true if the process is currently attached.
  bool IsAttached() const;

  // Returns the process handle. This handle is owned and managed by this
  // Process instance, thus the caller should not close the handle.
  mx_handle_t handle() const { return debug_handle_.get(); }

  // Returns the thread with the thread ID |thread_id| that's owned by this
  // process. Returns nullptr if no such thread exists. The returned pointer is
  // owned and managed by this Process instance.
  // NOTE: The returned thread might be stale. Call RefreshAllThreads()
  // beforehand if this is a problem.
  Thread* FindThreadById(mx_koid_t thread_id);

  // Returns an arbitrary thread that is owned by this process. This picks the
  // first thread that is returned from mx_object_get_info for the
  // MX_INFO_PROCESS_THREADS topic. This will refresh all threads.
  Thread* PickOneThread();

  // Refreshes the complete Thread list for this process. Returns false if an
  // error is returned from a syscall.
  bool RefreshAllThreads();

  // Iterates through all cached threads and invokes |callback| for each of
  // them. |callback| is guaranteed to get called only before ForEachThread()
  // returns, so it is safe to bind local variables to |callback|.
  using ThreadCallback = std::function<void(Thread*)>;
  void ForEachThread(const ThreadCallback& callback);

 private:
  Process() = default;

  // The exception handler invoked by ExceptionPort.
  void OnException(const mx_excp_type_t type,
                   const mx_exception_context_t& context);

  // The server that owns us.
  Server* server_;  // weak

  // The argv that this process was initialized with.
  std::vector<std::string> argv_;

  // The launchpad_t instance used to bootstrap and run the process. The Process
  // owns this instance and holds on to it until it gets destroyed.
  launchpad_t* launchpad_;

  // The debug-capable handle that we use to invoke mx_debug_* syscalls.
  mtl::UniqueHandle debug_handle_;

  // The key we receive after binding an exception port.
  ExceptionPort::Key eport_key_;

  // True, if the inferior has been run via a call to Start().
  bool started_;

  // The threads owned by this process. This is map is populated lazily when
  // threads are requested through FindThreadById() but gets fully refreshed
  // through PickOneThread() and RefreshAllThreads().
  //
  // TODO(armansito): Currently the contents of |threads_| can be stale unless
  // RefreshAllThreads() gets called frequently. Once we have support for thread
  // lifecycle events we can partially update the mapping based on events.
  using ThreadMap = std::unordered_map<mx_koid_t, std::unique_ptr<Thread>>;
  ThreadMap threads_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Process);
};

}  // namespace debugserver
