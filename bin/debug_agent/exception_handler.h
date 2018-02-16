// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <zx/event.h>
#include <zx/port.h>
#include <zx/process.h>
#include <zx/socket.h>
#include <zx/thread.h>

#include "garnet/lib/debug_ipc/stream_buffer.h"
#include "garnet/public/lib/fxl/synchronization/thread_annotations.h"

// This exception handler class runs a background thread that blocks on
// exceptions from processes being debugged. It also manages reading and
// writing on a socket for communication with the debugger client.
//
// This class will register as a StreamBuffer::Writer so commands sent on the
// socket_buffer() will be written to the socket to the debugger client.
class ExceptionHandler : public debug_ipc::StreamBuffer::Writer {
 public:
  class Sink {
   public:
    // Notification that there is new data to be read on the socket_buffer().
    virtual void OnStreamData() = 0;

    // Exception handlers.
    virtual void OnThreadStarting(const zx::thread& thread) = 0;
    virtual void OnThreadExiting(const zx::thread& thread) = 0;
  };

  ExceptionHandler();
  ~ExceptionHandler();

  debug_ipc::StreamBuffer& socket_buffer() { return socket_buffer_; }

  // Sets the sink for data and decoded exceptions. Setting this is not
  // threadsafe so this must be set before Start() is called, and the pointer
  // must remain valid until Shutdown() returns.
  void set_sink(Sink* sink) { sink_ = sink; }

  // Starts listening for exceptions and socket data. set_sink() must have been
  // called prior to this so that the data has a place to go.
  bool Start(zx::socket socket);

  // Blocks until the debugged programs have exited. The current sink will be
  // cleared.
  void Shutdown();

  // Attaches the exception handler to the given process. It must already have
  // been Start()ed.
  bool Attach(zx::process&& process);

  // StreamBuffer::Writer implementation.
  size_t ConsumeStreamBufferData(const char* data, size_t len) override;

 private:
  struct DebuggedProcess;

  // Implements the background thread.
  void DoThread();

  void OnSocketReadable();

  void OnProcessTerminated(const zx_port_packet_t& packet);

  // Handlers.
  void OnGeneralException(const zx_port_packet_t& packet,
                          const zx::thread& thread);
  void OnFatalPageFault(const zx_port_packet_t& packet,
                        const zx::thread& thread);
  void OnUndefinedInstruction(const zx_port_packet_t& packet,
                              const zx::thread& thread);
  void OnSoftwareBreakpoint(const zx_port_packet_t& packet,
                            const zx::thread& thread);
  void OnHardwareBreakpoint(const zx_port_packet_t& packet,
                            const zx::thread& thread);
  void OnUnalignedAccess(const zx_port_packet_t& packet,
                         const zx::thread& thread);
  void OnThreadPolicyError(const zx_port_packet_t& packet,
                           const zx::thread& thread);

  // Looks up the given Koid in the processes_ vector, returning it if found,
  // nullptr on not.
  const DebuggedProcess* ProcessForKoid(zx_koid_t koid);

  Sink* sink_;  // Non-owning.

  // Reads and buffers commands from the client.
  zx::socket socket_;
  debug_ipc::StreamBuffer socket_buffer_;

  // This is a unique_ptr so that it can be started explicitly in Start(),
  // giving time to do initialization while single-threaded.
  std::unique_ptr<std::thread> thread_;
  zx::port port_;

  // Signaling this event will cause the background thread to quit.
  zx::event quit_event_;

  // The list of all debugged processes. Protected by the Mutex. This uses
  // pointers so the DebuggedProcess data is stable across mutations.
  std::mutex mutex_;
  std::vector<std::unique_ptr<DebuggedProcess>> processes_
      FXL_GUARDED_BY(mutex_);
};
