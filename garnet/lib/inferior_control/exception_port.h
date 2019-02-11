// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/port.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace inferior_control {

class Thread;

// Maintains a dedicated thread for listening to exceptions from multiple
// processes and provides an interface that processes can use to subscribe to
// exception notifications.
class ExceptionPort final {
 public:
  // A Key is vended as a result of a call to Bind()
  using Key = uint64_t;

  // Handler callback invoked when the kernel reports an exception. For more
  // information about the possible values and fields of the |type| and
  // |context| parameters, see <zircon/syscalls/exception.h>.
  using Callback = fit::function<void(const zx_port_packet_t& packet,
                                      const zx_exception_context_t& context)>;

  explicit ExceptionPort(async_dispatcher_t* dispatcher);
  ~ExceptionPort();

  // Creates an exception port and starts waiting for events on it in a special
  // thread. Returns false if there is an error during set up.
  bool Run();

  // Quits the listening loop, closes the exception port and joins the
  // underlying thread. This must be called AFTER a successful call to Run().
  void Quit();

  // Binds an exception port to |process_handle| and associates |callback|
  // with it. The returned key can be used to unbind this process later.
  // On success, a positive Key value will be returned. On failure, 0 will be
  // returned.
  //
  // The |callback| will be posted on the origin thread's message loop, where
  // the origin thread is the thread on which this ExceptionPort instance was
  // created.
  //
  // This must be called AFTER a successful call to Run().
  Key Bind(const zx_handle_t process_handle, Callback callback);

  // Unbinds a previously bound exception port and returns true on success.
  // This must be called AFTER a successful call to Run().
  bool Unbind(const Key key);

 private:
  struct BindData {
    BindData() = default;
    BindData(zx_handle_t process_handle, zx_koid_t process_koid,
             Callback callback)
        : process_handle(process_handle),
          process_koid(process_koid),
          callback(std::move(callback)) {}

    zx_handle_t process_handle;
    zx_koid_t process_koid;
    Callback callback;
  };

  // Counter used for generating keys.
  static Key g_key_counter;

  // Currently resuming from exceptions requires the exception port handle.
  // This is solely for the benefit of |Process,Thread|.
  // TODO(PT-105): Delete when resuming from exceptions no longer requires the
  // eport handle.
  friend class Process;
  friend class Thread;
  zx_handle_t handle() const { return eport_.get(); }

  // The worker function.
  void Worker();

  // Set to false by Quit(). This tells |port_thread_| whether it should
  // terminate its loop as soon as zx_port_wait returns.
  std::atomic_bool keep_running_;

  // The origin dispatcher to post observer callback events to the thread
  // that created this object.
  async_dispatcher_t* const origin_dispatcher_;

  // The exception port used to bind to the inferior.
  // Once created it stays valid until |port_thread_| exits.
  zx::port eport_;

  // The thread on which we wait on the exception port.
  std::thread port_thread_;

  // All callbacks that are currently bound to this port.
  std::unordered_map<Key, BindData> callbacks_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ExceptionPort);
};

}  // namespace inferior_control
