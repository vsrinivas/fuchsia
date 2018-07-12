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

namespace debugserver {

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

  // The worker function.
  void Worker();

  // Set to false by Quit(). This tells |io_thread_| whether it should terminate
  // its loop as soon as zx_port_wait returns.
  std::atomic_bool keep_running_;

  // The origin dispatcher to post observer callback events to the thread
  // that created this object.
  async_dispatcher_t* const origin_dispatcher_;

  // The exception port handle and a mutex for synchronizing access to it.
  // |io_thread_| only ever reads from |eport_handle_| but a call to Quit() can
  // set it to 0. This can really only happen if Quit() is called before
  // Worker() even runs on the |io_thread_| which is extremely unlikely. But we
  // play safe anyway.
  std::mutex eport_mutex_;
  zx::port eport_handle_;

  // The thread on which we wait on the exception port.
  std::thread io_thread_;

  // All callbacks that are currently bound to this port.
  std::unordered_map<Key, BindData> callbacks_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ExceptionPort);
};

// Print an exception in user-friendly form.
// This is for log messages and interactive programs that wish to report
// the exception.
// This doesn't have a better place at the moment.
void PrintException(FILE* out, const Thread* thread, zx_excp_type_t type,
                    const zx_exception_context_t& context);

// Print a signal (or signals) in user-friendly form.
// This is for log messages and interactive programs that wish to report
// the exception.
// This doesn't have a better place at the moment.
void PrintSignal(FILE* out, const Thread* thread, zx_signals_t signals);

}  // namespace debugserver
