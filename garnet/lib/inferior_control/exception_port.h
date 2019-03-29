// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/memory/ref_ptr.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

namespace inferior_control {

class Thread;

// Maintains a dedicated thread for listening for exceptions and signals
// from multiple processes and provides an interface that processes can use
// to subscribe to exception/signal notifications (including their threads).
class ExceptionPort final {
 public:
  // A Key is vended as a result of a call to Bind()
  using Key = uint64_t;

  // Callback invoked when a packet is received.
  using PacketCallback = fit::function<void(const zx_port_packet_t& packet)>;

  explicit ExceptionPort(async_dispatcher_t* dispatcher,
                         PacketCallback exception_callback,
                         PacketCallback signal_callback);
  ~ExceptionPort();

  // Creates an exception port and starts waiting for events on it in a special
  // thread. Returns false if there is an error during set up.
  bool Run();

  // Quits the listening loop, closes the exception port and joins the
  // underlying thread. This must be called AFTER a successful call to Run().
  void Quit();

  // Binds an exception port to |process| using key |key|.
  // Returns true on success.
  //
  // The callbacks will be posted on |dispatcher| (args to our constructor).
  //
  // This must be called AFTER a successful call to Run().
  bool Bind(const zx::process& process, Key key);

  // Unbinds a previously bound exception port and returns true on success.
  // This must be called AFTER a successful call to Run().
  bool Unbind(const zx::process& process, Key key);

  // Async-wait for signals on |thread|.
  // The signals we wait for is determined by the thread's current state.
  // If it's running we wait for it to be suspended (or terminated).
  // If it's suspended we wait for it to be running (or terminated).
  void WaitAsync(Thread* thread);

 private:
  // Currently resuming from exceptions requires the exception port handle.
  // This is solely for the benefit of |Server,Thread|.
  // TODO(PT-105): Delete when resuming from exceptions no longer requires the
  // eport handle.
  friend class Server;
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

  // The functions to handle exceptions and signals.
  PacketCallback exception_callback_;
  PacketCallback signal_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ExceptionPort);
};

}  // namespace inferior_control
