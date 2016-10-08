// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <memory>

#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

#include "command_handler.h"
#include "process.h"
#include "thread.h"

namespace debugserver {

// Server implements the main loop and handles commands received over a TCP port
// (from gdb or lldb).
class Server final {
 public:
  explicit Server(uint16_t port);
  ~Server() = default;

  // Starts the main loop. This will first block and wait for an incoming
  // connection. Once there is a connection, this will start an event loop for
  // handling commands.
  //
  // Returns when the main loop exits (e.g. due to a closed client connection).
  // Returns true if the main loop exits cleanly, or false in the case of an
  // error.
  // TODO(armansito): More clearly define the error scenario.
  bool Run();

  // Returns a raw pointer to the current inferior. The instance pointed to by
  // the returned pointer is owned by this Server instance and should not be
  // deleted.
  Process* current_process() const { return current_process_.get(); }

  // Sets the current process. This cleans up the current process (if any) and
  // takes ownership of |process|.
  void set_current_process(Process* process) {
    current_process_.reset(process);
  }

  // Returns a raw pointer to the current thread.
  Thread* current_thread() const { return current_thread_.get(); }

  // Assigns the current thread.
  void SetCurrentThread(Thread* thread);

 private:
  // Maximum number of characters in inbound/outbound buffers.
  constexpr static size_t kMaxBufferSize = 4096;

  Server() = default;

  // Listens for incoming connections on port |port_|. Once a connection is
  // accepted, returns true and stores the client socket in |client_sock_|,
  // and the server socket in |server_sock_|. Returns false if an error occurs.
  bool Listen();

  // Send an acknowledgment packet. If |ack| is true, then a '+' ACK will be
  // sent to indicate that a packet was received correctly, or '-' to request
  // retransmission. Returns false if there is an error while writing to the
  // socket. This method blocks until the syscall to write to the socket
  // returns.
  bool SendAck(bool ack);

  // Posts an asynchronous task on the message loop to listen for an incoming
  // packet.
  void PostReadTask();

  // Posts an asynchronous task on the message loop to send a packet over the
  // wire.
  void PostWriteTask(const uint8_t* rsp, size_t rsp_bytes);

  // Sets the run status and quits the main message loop.
  void QuitMessageLoop(bool status);

  // TCP port number that we will listen on.
  uint16_t port_;

  // File descriptors for the sockets used for listening for incoming
  // connections (e.g. from gdb or lldb) and for the actual communication.
  // |client_sock_| is used for GDB Remote Protocol communication.
  ftl::UniqueFD client_sock_;
  ftl::UniqueFD server_sock_;

  // Buffers used for reading/writing incoming/outgoing bytes.
  std::array<uint8_t, kMaxBufferSize> in_buffer_;
  std::array<uint8_t, kMaxBufferSize> out_buffer_;

  // The CommandHandler that is responsible for interpreting received command
  // packets and routing them to the correct handler.
  CommandHandler command_handler_;

  // Strong pointer to the current inferior process that is being debugged.
  std::unique_ptr<Process> current_process_;

  // The current thread under debug. We only keep a weak pointer here, since the
  // instance itself is owned by a Process and may get removed.
  ftl::WeakPtr<Thread> current_thread_;

  // The main loop.
  mtl::MessageLoop message_loop_;

  // Stores the global error state. This is used to determine the return value
  // for "Run()" when |message_loop_| exits.
  bool run_status_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Server);
};

}  // namespace debugserver
