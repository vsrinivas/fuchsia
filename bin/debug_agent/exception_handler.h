// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/event.h>
#include <zx/port.h>
#include <zx/process.h>
#include <zx/socket.h>
#include <zx/thread.h>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "garnet/bin/debug_agent/handle_read_watcher.h"
#include "garnet/lib/debug_ipc/stream_buffer.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_annotations.h"

// This exception handler class runs a background thread that blocks on
// exceptions from processes being debugged. It also manages reading and
// writing on a socket for communication with the debugger client.
//
// Start() and Shutdown() can be called from any thread, but all other
// functions must be called only on the background thread that the exception
// handler creates and dispatches its notifications on. It is not threadsafe.
//
// This class will register as a StreamBuffer::Writer so commands sent on the
// socket_buffer() will be written to the socket to the debugger client.
class ExceptionHandler : public debug_ipc::StreamBuffer::Writer {
 public:
  class ProcessWatcher {
   public:
    // Notification that the process is terminated. The implementation should
    // call Detach() on the handle.
    virtual void OnProcessTerminated(zx_koid_t process_koid) = 0;

    // Exception handlers.
    virtual void OnThreadStarting(zx::thread thread,
                                  zx_koid_t process_koid,
                                  zx_koid_t thread_koid) = 0;
    virtual void OnThreadExiting(zx_koid_t proc_koid,
                                 zx_koid_t thread_koid) = 0;
    virtual void OnException(zx_koid_t proc_koid,
                             zx_koid_t thread_koid,
                             uint32_t type) = 0;
  };

  ExceptionHandler();
  ~ExceptionHandler();

  debug_ipc::StreamBuffer& socket_buffer() { return socket_buffer_; }

  // Sets the sinks for data and decoded process exceptions. Setting these are
  // not threadsafe so this must be set before Start() is called, and the
  // pointers must remain valid until Shutdown() returns.
  void set_read_watcher(HandleReadWatcher* w) { read_watcher_ = w; }
  void set_process_watcher(ProcessWatcher* w) { process_watcher_ = w; }

  // Starts listening for exceptions and socket data. set_sink() must have been
  // called prior to this so that the data has a place to go.
  bool Start(zx::socket socket);

  // Blocks until the debugged programs have exited. The current sink will be
  // cleared.
  void Shutdown();

  // Attaches the exception handler to the given process. It must already have
  // been Start()ed. Ownership of the handle is not transferred, it must
  // remain valid until Detach() is called.
  bool Attach(zx_koid_t koid, zx_handle_t process);

  // Detaches the exception handler.
  void Detach(zx_koid_t koid);

  // StreamBuffer::Writer implementation. Sends data to the client.
  size_t ConsumeStreamBufferData(const char* data, size_t len) override;

 private:
  struct WatchedProcess;

  // Implements the background thread.
  void DoThread();

  void OnSocketReadable();

  void OnProcessTerminated(const zx_port_packet_t& packet);

  // Looks up the given Koid in the processes_ vector, returning it if found,
  // nullptr on not.
  const WatchedProcess* WatchedProcessForKoid(zx_koid_t koid);

  HandleReadWatcher* read_watcher_ = nullptr;  // Non-owning.
  ProcessWatcher* process_watcher_ = nullptr;  // Non-owning.

  // Reads and buffers commands from the client.
  zx::socket socket_;
  debug_ipc::StreamBuffer socket_buffer_;

  // This is a unique_ptr so that it can be started explicitly in Start(),
  // giving time to do initialization while single-threaded.
  std::unique_ptr<std::thread> thread_;
  zx::port port_;

  // Signaling this event will cause the background thread to quit.
  zx::event quit_event_;

  // Uses pointers so the DebuggedProcess data is stable across mutations.
  std::vector<std::unique_ptr<WatchedProcess>> processes_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ExceptionHandler);
};
