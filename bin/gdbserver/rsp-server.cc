// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rsp-server.h"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <array>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

#include "rsp-util.h"
#include "stop-reply-packet.h"
#include "util.h"

namespace debugserver {

namespace {

constexpr char kStopNotification[] = "Stop";
constexpr char kStopAck[] = "vStopped";

}  // namespace

RspServer::PendingNotification::PendingNotification(
    const ftl::StringView& name,
    const ftl::StringView& event,
    const ftl::TimeDelta& timeout)
  : name(name.data(), name.size()),
    event(event.data(), event.size()),
    timeout(timeout) {}

RspServer::RspServer(uint16_t port)
    : port_(port),
      server_sock_(-1),
      command_handler_(this) {}

bool RspServer::Run() {
  FTL_DCHECK(!io_loop_);

  // Listen for an incoming connection.
  if (!Listen())
    return false;

  // |client_sock_| should be ready to be consumed now.
  FTL_DCHECK(client_sock_.is_valid());

  if (!exception_port_.Run()) {
    FTL_LOG(ERROR) << "Failed to initialize exception port!";
    return false;
  }

  io_loop_ = std::make_unique<RspIOLoop>(client_sock_.get(), this);
  io_loop_->Run();

  // Start the main loop.
  message_loop_.Run();

  FTL_LOG(INFO) << "Main loop exited";

  // Tell the I/O loop to quit its message loop and wait for it to finish.
  io_loop_->Quit();

  // Tell the exception port to quit and wait for it to finish.
  exception_port_.Quit();

  return run_status_;
}

void RspServer::QueueNotification(const ftl::StringView& name,
                                  const ftl::StringView& event,
                                  const ftl::TimeDelta& timeout) {
  // The GDB Remote protocol defines only the "Stop" notification
  FTL_DCHECK(name == kStopNotification);

  FTL_VLOG(1) << "Preparing notification: " << name << ":" << event;

  notify_queue_.push(
      std::make_unique<PendingNotification>(name, event, timeout));
  TryPostNextNotification();
}

void RspServer::QueueStopNotification(const ftl::StringView& event,
                                      const ftl::TimeDelta& timeout) {
  QueueNotification(kStopNotification, event, timeout);
}

bool RspServer::Listen() {
  FTL_DCHECK(!server_sock_.is_valid());
  FTL_DCHECK(!client_sock_.is_valid());

  ftl::UniqueFD server_sock(socket(AF_INET, SOCK_STREAM, 0));
  if (!server_sock.is_valid()) {
    util::LogErrorWithErrno("Failed to open socket");
    return false;
  }

  // Bind to a local address for listening.
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);

  if (bind(server_sock.get(), (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    util::LogErrorWithErrno("Failed to bind socket");
    return false;
  }

  if (listen(server_sock.get(), 1) < 0) {
    util::LogErrorWithErrno("Listen failed");
    return false;
  }

  FTL_LOG(INFO) << "Waiting for a connection on port " << port_ << "...";

  // Reuse |addr| here for the destination address.
  socklen_t addrlen = sizeof(addr);
  ftl::UniqueFD client_sock(
      accept(server_sock.get(), (struct sockaddr*)&addr, &addrlen));
  if (!client_sock.is_valid()) {
    util::LogErrorWithErrno("Accept failed");
    return false;
  }

  FTL_LOG(INFO) << "Client connected";

  server_sock_ = std::move(server_sock);
  client_sock_ = std::move(client_sock);

  return true;
}

void RspServer::SendAck(bool ack) {
  // TODO(armansito): Don't send anything if we're in no-acknowledgment mode. We
  // currently don't support this mode.
  FTL_DCHECK(io_loop_);
  char payload = ack ? '+' : '-';
  io_loop_->PostWriteTask(ftl::StringView(&payload, 1));
}

void RspServer::PostWriteTask(bool notify, const ftl::StringView& data) {
  FTL_DCHECK(io_loop_);
  FTL_DCHECK(data.size() + 4 < kMaxBufferSize);

  // Copy the data into a std::string to capture it in the closure.
  message_loop_.task_runner()->PostTask(
      [ this, data = data.ToString(), notify ] {
        int index = 0;
        out_buffer_[index++] = notify ? '%' : '$';
        memcpy(out_buffer_.data() + index, data.data(), data.size());
        index += data.size();
        out_buffer_[index++] = '#';

        uint8_t checksum = 0;
        for (uint8_t byte : data)
          checksum += byte;

        util::EncodeByteString(checksum, out_buffer_.data() + index);
        index += 2;

        io_loop_->PostWriteTask(ftl::StringView(out_buffer_.data(), index));
      });
}

void RspServer::PostPacketWriteTask(const ftl::StringView& data) {
  PostWriteTask(false, data);
}

void RspServer::PostPendingNotificationWriteTask() {
  FTL_DCHECK(pending_notification_);
  std::string bytes(pending_notification_->name + ":" +
                    pending_notification_->event);
  PostWriteTask(true, bytes);
}

bool RspServer::TryPostNextNotification() {
  if (pending_notification_ || notify_queue_.empty())
    return false;

  pending_notification_ = std::move(notify_queue_.front());
  notify_queue_.pop();
  FTL_DCHECK(pending_notification_);

  // Send the notification.
  PostPendingNotificationWriteTask();
  PostNotificationTimeoutHandler();
  return true;
}

void RspServer::PostNotificationTimeoutHandler() {
  // Set up a timeout handler.
  // Continually resend the notification until the remote end acknowledges it,
  // or until the notification is removed (say because the process exits).
  message_loop_.task_runner()->PostDelayedTask(
      [this, pending = pending_notification_.get()] {
        // If the notification that we set this timeout for has already been
        // acknowledged by the remote, then we have nothing to do.
        // TODO(dje): sequence numbers?
        if (pending_notification_.get() != pending)
          return;

        FTL_LOG(WARNING) << "Notification timed out; retrying";
        PostPendingNotificationWriteTask();
        PostNotificationTimeoutHandler();
      },
      pending_notification_->timeout);
}

void RspServer::OnBytesRead(const ftl::StringView& bytes_read) {
  // If this is a packet acknowledgment then ignore it and read again.
  // TODO(armansito): Re-send previous packet if we got "-".
  if (bytes_read == "+")
    return;

  ftl::StringView packet_data;
  bool verified = util::VerifyPacket(bytes_read, &packet_data);

  // Send acknowledgment back
  SendAck(verified);

  // Wait for the next command if we requested retransmission
  if (!verified)
    return;

  // Before anything else, check to see if this is an acknowledgment in
  // response to a notification. The GDB Remote protocol defines only the
  // "Stop" notification, so we specially handle its acknowledgment here.
  if (packet_data == kStopAck) {
    if (pending_notification_) {
      FTL_VLOG(2) << "Notification acknowledged";

      // At this point we enter a loop of passing all queued notifications
      // to GDB as normal (ack'd) messages, terminating with "OK". Nothing else
      // is exchanged until this loop completes.
      // https://sourceware.org/gdb/current/onlinedocs/gdb/Notification-Packets.html
      // This is awkward to do given our message loop. What we do is keep
      // the original notification around as a flag indicating this loop is
      // active until the queue is empty.
      // TODO(dje): Redo this.
      if (!notify_queue_.empty()) {
        std::unique_ptr<PendingNotification> notif =
            std::move(notify_queue_.front());
        notify_queue_.pop();
        PostPacketWriteTask(notif->event);
      } else {
        pending_notification_.reset();
        PostPacketWriteTask("OK");
      }
    } else {
      FTL_VLOG(2) << "Notification acknowledged, but notification gone";
    }
    return;
  }

  // Route the packet data to the command handler.
  auto callback = [this](const ftl::StringView& rsp) {
    // Send the response if there is one.
    PostPacketWriteTask(rsp);
  };

  // If the command is handled, then |callback| will be called at some point,
  // so we're done.
  if (command_handler_.HandleCommand(packet_data, callback))
    return;

  // If the command wasn't handled, that's because we do not support it, so we
  // respond with an empty response and continue.
  FTL_LOG(ERROR) << "Command not supported: " << packet_data;
  callback("");
}

void RspServer::OnDisconnected() {
  // Exit successfully in the case of a remote disconnect.
  FTL_LOG(INFO) << "Client disconnected";
  QuitMessageLoop(true);
}

void RspServer::OnIOError() {
  FTL_LOG(ERROR) << "An I/O error has occurred. Exiting the main loop";
  QuitMessageLoop(false);
}

void RspServer::OnThreadStarting(Process* process,
                                 Thread* thread,
                                 const mx_exception_context_t& context) {
  FTL_DCHECK(process);

  // TODO(armansito): We send a stop-reply packet for the new thread. This
  // inherently completes any pending vRun sequence but technically shouldn't be
  // sent unless GDB enables QThreadEvents. Add some logic here to send this
  // conditionally only when necessary.
  StopReplyPacket stop_reply(StopReplyPacket::Type::kReceivedSignal);
  stop_reply.SetSignalNumber(5);
  stop_reply.SetThreadId(process->id(), context.tid);
  stop_reply.SetStopReason("create");

  auto packet = stop_reply.Build();

  switch (process->state()) {
    case Process::State::kStarting:
      // vRun receives a synchronous response. After that it's all asynchronous.
      PostPacketWriteTask(ftl::StringView(packet.data(), packet.size()));
      process->set_state(Process::State::kRunning);
      break;
    case Process::State::kRunning:
      QueueStopNotification(ftl::StringView(packet.data(), packet.size()));
      break;
    default:
      FTL_DCHECK(false);
  }
}

void RspServer::OnThreadExiting(Process* process,
                                Thread* thread,
                                const mx_excp_type_t type,
                                const mx_exception_context_t& context) {
  std::vector<char> packet;
  FTL_LOG(INFO) << "Thread " << thread->GetName() << " exited";
  int exit_code = 0; // TODO(dje)
  StopReplyPacket stop_reply(StopReplyPacket::Type::kThreadExited);
  stop_reply.SetSignalNumber(exit_code);
  stop_reply.SetThreadId(process->id(), thread->id());
  packet = stop_reply.Build();
  QueueStopNotification(ftl::StringView(packet.data(), packet.size()));

  // The Remote Serial Protocol doesn't provide for a means to examine
  // state when exiting, like it does when starting. The thread needs to be
  // "resumed" so that the o/s will finish terminating the thread. This also
  // takes care of marking the thread as kGone.
  thread->ResumeForExit();
}

void RspServer::OnProcessExit(Process* process,
                              const mx_excp_type_t type,
                              const mx_exception_context_t& context) {
  std::vector<char> packet;
  FTL_LOG(INFO) << "Process " << process->GetName() << " exited";
  SetCurrentThread(nullptr);
  int exit_code = process->ExitCode();
  StopReplyPacket stop_reply(StopReplyPacket::Type::kProcessExited);
  stop_reply.SetSignalNumber(exit_code);
  packet = stop_reply.Build();
  QueueStopNotification(ftl::StringView(packet.data(), packet.size()));
}

void RspServer::OnArchitecturalException(
    Process* process,
    Thread* thread,
    const mx_excp_type_t type,
    const mx_exception_context_t& context) {
  FTL_DCHECK(process);
  FTL_DCHECK(thread);
  FTL_VLOG(1) << "Architectural Exception: "
              << util::ExceptionToString(type, context);

  // TODO(armansito): Fine-tune this check if we ever support multi-processing.
  FTL_DCHECK(process == current_process());

  int sigval = thread->GetGdbSignal();
  if (sigval < 0) {
    FTL_LOG(ERROR) << "Exception reporting not supported on current "
                   << "architecture!";
    return;
  }

  FTL_DCHECK(sigval < std::numeric_limits<uint8_t>::max());

  StopReplyPacket stop_reply(StopReplyPacket::Type::kReceivedSignal);
  stop_reply.SetSignalNumber(sigval);
  stop_reply.SetThreadId(process->id(), context.tid);

  // Registers.
  if (thread->registers()->RefreshGeneralRegisters()) {
    std::array<int, 3> regnos{{arch::GetFPRegisterNumber(),
                               arch::GetSPRegisterNumber(),
                               arch::GetPCRegisterNumber()}};

    for (int regno : regnos) {
      FTL_DCHECK(regno < std::numeric_limits<uint8_t>::max() && regno >= 0);
      std::string regval = thread->registers()->GetRegisterAsString(regno);
      stop_reply.AddRegisterValue(regno, regval);
    }
  } else {
    FTL_LOG(WARNING)
        << "Couldn't read thread registers while handling exception";
  }

  auto packet = stop_reply.Build();
  QueueStopNotification(ftl::StringView(packet.data(), packet.size()));
}

}  // namespace debugserver
