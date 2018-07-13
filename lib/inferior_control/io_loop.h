// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <atomic>
#include <thread>

#include <lib/async-loop/cpp/loop.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/fxl/tasks/task_runner.h"

namespace inferior_control {

// Maintains dedicated threads for reads and writes on a given socket file
// descriptor and allows read and write tasks to be scheduled from a single
// origin thread.
//
// This class is thread-safe as long as all the public methods are accessed from
// the thread that initialized this instance.
//
// TODO(armansito): This is a temporary solution until there is a
// fdio_get_handle (or equivalent) interface to get a zx_handle_t from socket
// fd. That way we can avoid blocking reads and writes while also using a single
// thread. Then again this works fine too.
class IOLoop {
 public:
  // Delegate class for receiving asynchronous events for the result of
  // read/write operations. All operations will be posted on the MessageLoop of
  // the thread on which the IOLoop object was created.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when new bytes have been read from the socket.
    virtual void OnBytesRead(const fxl::StringView& bytes_read) = 0;

    // Called when the remote end closes the TCP connection.
    virtual void OnDisconnected() = 0;

    // Called when there is an error in either the read or write tasks.
    virtual void OnIOError() = 0;
  };

  // Does not take ownership of any of the parameters. Care should be taken to
  // make sure that |delegate| and |fd| outlive this object.
  IOLoop(int fd, Delegate* delegate, async::Loop* origin_loop);

  // The destructor calls Quit() and thus it may block.
  virtual ~IOLoop();

  // Initializes the underlying threads and message loops and runs them.
  void Run();

  // Quits the underlying message loops and block until the underlying threads
  // complete their tasks and join. Since the threads do blocking work
  // (read/write) this may block until either pending read and/or write returns.
  void Quit();

  // Posts an asynchronous task on the message loop to send a packet.
  void PostWriteTask(const fxl::StringView& bytes);

 protected:
  bool quit_called() const { return quit_called_; }
  int fd() const { return fd_; }
  Delegate* delegate() const { return delegate_; }
  async_dispatcher_t* origin_dispatcher() const {
    return origin_loop_->dispatcher();
  }
  async_dispatcher_t* read_dispatcher() const {
    return read_loop_.dispatcher();
  }
  async_dispatcher_t* write_dispatcher() const {
    return write_loop_.dispatcher();
  }

  // Helper method for StartReadTask, only called from the read thread.
  // Process one read request.
  virtual void OnReadTask() = 0;

  // Notifies the delegate that there has been an I/O error.
  void ReportError();
  void ReportDisconnected();

 private:
  IOLoop() = default;

  // True if Quit() was called. This tells the |read_thread| to terminate its
  // loop as soon as any blocking call to read returns.
  std::atomic_bool quit_called_;

  // The socket file descriptor.
  int fd_;

  // The delegate that we send I/O events to.
  Delegate* delegate_;

  // True, if Run() has been called.
  bool is_running_;

  // The origin loop used to post delegate events to the thread that created
  // this object.
  async::Loop* origin_loop_;

  // The message loops for running on two threads for I/O respectively.
  async::Loop read_loop_;
  async::Loop write_loop_;

  FXL_DISALLOW_COPY_AND_ASSIGN(IOLoop);
};

}  // namespace inferior_control
