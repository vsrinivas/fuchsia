// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <memory>
#include <queue>

#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"
#include "lib/mtl/tasks/message_loop.h"

#include "command-handler.h"
#include "exception-port.h"
#include "io-loop.h"
#include "process.h"
#include "thread.h"

namespace debugserver {

// Server implements the main loop and handles commands received over a TCP port
// (from gdb or lldb).
//
// NOTE: This class is generally not thread safe. Care must be taken when
// calling methods such as set_current_thread(), SetCurrentThread(), and
// QueueNotification() which modify the internal state of a Server instance.
class Server final : public IOLoop::Delegate, public Process::Delegate {
 public:
  // The default timeout interval used when sending notifications.
  constexpr static int64_t kDefaultTimeoutSeconds = 30;

  explicit Server(uint16_t port);
  ~Server();

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

  // Returns a mutable reference to the main message loop. The returned instance
  // is owned by this Server instance and should not be deleted.
  mtl::MessageLoop* message_loop() { return &message_loop_; }

  // Returns a mutable reference to the exception port. The returned instance is
  // owned by this Server instance and should not be deleted.
  ExceptionPort* exception_port() { return &exception_port_; }

  // Queue a notification packet and send it out if there are no currently
  // queued notifications. The GDB Remote Protocol defines a specific
  // control-flow for notification packets, such that each notification packet
  // will be pending until the remote end acknowledges it. There can be only
  // one pending notification at a time.
  //
  // A notification will time out if the remote end does not acknowledge it
  // within |timeout|. If a notification times out, it will be sent again.
  void QueueNotification(
      const ftl::StringView& name,
      const ftl::StringView& event,
      const ftl::TimeDelta& timeout =
          ftl::TimeDelta::FromSeconds(kDefaultTimeoutSeconds));

  // Wrapper of QueueNotification for "Stop" notifications.
  void QueueStopNotification(
      const ftl::StringView& event,
      const ftl::TimeDelta& timeout =
          ftl::TimeDelta::FromSeconds(kDefaultTimeoutSeconds));

  // Call this to schedule termination of gdbserver.
  // Any outstanding messages will be sent first.
  // N.B. The Server will exit its main loop asynchronously so any
  // subsequently posted tasks will be dropped.
  void PostQuitMessageLoop(bool status);

 private:
  // Maximum number of characters in the outbound buffer.
  constexpr static size_t kMaxBufferSize = 4096;

  // Represents a pending notification packet.
  struct PendingNotification {
    PendingNotification(const ftl::StringView& name,
                        const ftl::StringView& event,
                        const ftl::TimeDelta& timeout);

    std::string name;
    std::string event;
    ftl::TimeDelta timeout;
  };

  Server() = default;

  // Listens for incoming connections on port |port_|. Once a connection is
  // accepted, returns true and stores the client socket in |client_sock_|,
  // and the server socket in |server_sock_|. Returns false if an error occurs.
  bool Listen();

  // Send an acknowledgment packet. If |ack| is true, then a '+' ACK will be
  // sent to indicate that a packet was received correctly, or '-' to request
  // retransmission. This method blocks until the syscall to write to the socket
  // returns.
  void SendAck(bool ack);

  // Posts an asynchronous task on the message loop to send a packet over the
  // wire. |data| will be wrapped in a GDB Remote Protocol packet after
  // computing the checksum. If |notify| is true, then a notification packet
  // will be sent (where the first byte of the packet equals '%'), otherwise a
  // regular packet will be sent (first byte is '$').
  void PostWriteTask(bool notify, const ftl::StringView& data);

  // Convenience helpers for PostWriteTask
  void PostPacketWriteTask(const ftl::StringView& data);
  void PostPendingNotificationWriteTask();

  // If |pending_notification_| is NULL, this pops the next lined-up
  // notification from |notify_queue_| and assigns it as the new pending
  // notification and sends it to the remote device.
  //
  // Returns true, if the next notification was posted. Returns false if the
  // next notification was not posted because either there is still a pending
  // unacknowledged notification or the notification queue is empty.
  bool TryPostNextNotification();

  // Post a timeout handler for |pending_notification_|.
  void PostNotificationTimeoutHandler();

  // Sets the run status and quits the main message loop.
  void QuitMessageLoop(bool status);

  // IOLoop::Delegate overrides.
  void OnBytesRead(const ftl::StringView& bytes) override;
  void OnDisconnected() override;
  void OnIOError() override;

  // Process::Delegate overrides.
  void OnThreadStarted(Process* process,
                       Thread* thread,
                       const mx_exception_context_t& context) override;
  void OnProcessOrThreadExited(Process* process,
                               Thread* thread,
                               const mx_excp_type_t type,
                               const mx_exception_context_t& context) override;
  void OnArchitecturalException(Process* process,
                                Thread* thread,
                                const mx_excp_type_t type,
                                const mx_exception_context_t& context) override;

  // TCP port number that we will listen on.
  uint16_t port_;

  // File descriptors for the sockets used for listening for incoming
  // connections (e.g. from gdb or lldb) and for the actual communication.
  // |client_sock_| is used for GDB Remote Protocol communication.
  ftl::UniqueFD client_sock_;
  ftl::UniqueFD server_sock_;

  // Buffer used for writing outgoing bytes.
  std::array<char, kMaxBufferSize> out_buffer_;

  // The CommandHandler that is responsible for interpreting received command
  // packets and routing them to the correct handler.
  CommandHandler command_handler_;

  // The current thread under debug. We only keep a weak pointer here, since the
  // instance itself is owned by a Process and may get removed.
  ftl::WeakPtr<Thread> current_thread_;

  // The current queue of notifications that have not been sent out yet.
  std::queue<std::unique_ptr<PendingNotification>> notify_queue_;

  // The currently pending notification that has been sent out but has NOT been
  // acknowledged by the remote end yet.
  std::unique_ptr<PendingNotification> pending_notification_;

  // The main loop.
  mtl::MessageLoop message_loop_;

  // The IOLoop used for blocking I/O operations over |client_sock_|.
  // |message_loop_| and |client_sock_| both MUST outlive |io_loop_|. We take
  // care to clean it up in the destructor.
  std::unique_ptr<IOLoop> io_loop_;

  // The ExceptionPort used by inferiors to receive exceptions.
  // (This is declared after |message_loop_| since that needs to have been
  // created before this can be initialized).
  ExceptionPort exception_port_;

  // Strong pointer to the current inferior process that is being debugged.
  // NOTE: This must be declared after |exception_port_| above, since the
  // process may do work in its destructor to detach itself.
  std::unique_ptr<Process> current_process_;

  // Stores the global error state. This is used to determine the return value
  // for "Run()" when |message_loop_| exits.
  bool run_status_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Server);
};

}  // namespace debugserver
