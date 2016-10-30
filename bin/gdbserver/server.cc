// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "server.h"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <array>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/ftl/strings/string_printf.h"

#include "stop_reply_packet.h"
#include "util.h"

namespace debugserver {

namespace {

constexpr char kStopNotification[] = "Stop";
constexpr char kStopAck[] = "vStopped";

}  // namespace

Server::PendingNotification::PendingNotification(
    const std::string& bytes,
    const ftl::TimeDelta& timeout,
    size_t retries_left)
    : bytes(bytes), timeout(timeout), retries_left(retries_left) {}

Server::Server(uint16_t port)
    : port_(port),
      client_sock_(-1),
      server_sock_(-1),
      command_handler_(this),
      run_status_(true) {}

Server::~Server() {
  // This will invoke the IOLoop destructor which will clean up and join the I/O
  // threads.
  io_loop_.reset();
}

bool Server::Run() {
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

  io_loop_ = std::make_unique<IOLoop>(client_sock_.get(), this);
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

void Server::SetCurrentThread(Thread* thread) {
  if (!thread)
    current_thread_.reset();
  else
    current_thread_ = thread->AsWeakPtr();
}

void Server::SendNotification(
    const ftl::StringView& name,
    const ftl::StringView& event,
    const ftl::TimeDelta& timeout,
    size_t retry_count) {
  // The GDB Remote protocol defines only the "Stop" notification
  FTL_DCHECK(name == kStopNotification);
  std::string bytes = name.ToString() + ":" + event.ToString();

  FTL_VLOG(1) << "Preparing notification: " << bytes;

  notify_queue_.push(std::make_unique<PendingNotification>(
      bytes, timeout, retry_count));
  TryPostNextNotification();
}

bool Server::Listen() {
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

void Server::SendAck(bool ack) {
  // TODO(armansito): Don't send anything if we're in no-acknowledgment mode. We
  // currently don't support this mode.
  FTL_DCHECK(io_loop_);
  char payload = ack ? '+' : '-';
  io_loop_->PostWriteTask(ftl::StringView(&payload, 1));
}

void Server::PostWriteTask(bool notify, const ftl::StringView& data) {
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

void Server::PostPacketWriteTask(const ftl::StringView& data) {
  PostWriteTask(false, data);
}

void Server::PostNotificationWriteTask(const ftl::StringView& data) {
  PostWriteTask(true, data);
}

bool Server::TryPostNextNotification() {
  if (pending_notification_ || notify_queue_.empty())
    return false;

  pending_notification_ = std::move(notify_queue_.front());
  notify_queue_.pop();
  FTL_DCHECK(pending_notification_);

  // Send the notification
  PostNotificationWriteTask(pending_notification_->bytes);

  // Set up a timeout handler.
  // NOTE: There is a potential race in the logic below where the remote end
  // COULD send an acknowledgment right after the timeout has expired and we
  // have already sent the next queued up notification. In that case we could
  // potentially interpret the acknowledgment packet for the previous
  // notification as if it has been sent for the current one. The default time
  // out interval is defined to be long enough that this is very unlikely.
  message_loop_.task_runner()->PostDelayedTask(
      [this, pending = pending_notification_.get()] {
        // If the notification that we set this timeout for has already been
        // acknowledged by the remote, then we have nothing to do.
        if (pending_notification_.get() != pending)
          return;

        // If the notification has a positive retry count, then send it again.
        if (pending_notification_->retries_left-- > 0) {
          FTL_LOG(WARNING) << "Notification timed out; retrying";
          PostNotificationWriteTask(pending_notification_->bytes);
          return;
        }

        // The retry count has reached 0. Remove the pending notification and
        // queue up the next one.
        FTL_LOG(WARNING) << "Pending notifification timed out";
        pending_notification_.reset();
        TryPostNextNotification();
      },
      pending_notification_->timeout);

  return true;
}

void Server::QuitMessageLoop(bool status) {
  run_status_ = status;
  message_loop_.QuitNow();
}

void Server::OnBytesRead(const ftl::StringView& bytes_read) {
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
  if (packet_data == kStopAck && pending_notification_) {
    FTL_LOG(INFO) << "Notification acknowledged";
    pending_notification_.reset();

    // Try to post the next notification. If there are no queued events report,
    // reply with OK.
    if (!TryPostNextNotification()) {
      FTL_DCHECK(notify_queue_.empty());
      PostPacketWriteTask("OK");
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

void Server::OnDisconnected() {
  // Exit successfully in the case of a remote disconnect.
  FTL_LOG(INFO) << "Client disconnected";
  QuitMessageLoop(true);
}

void Server::OnIOError() {
  FTL_LOG(ERROR) << "An I/O error has occurred. Exiting the main loop";
  QuitMessageLoop(false);
}

void Server::OnThreadStarted(Process* process,
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
  PostPacketWriteTask(ftl::StringView(packet.data(), packet.size()));
}

void Server::OnProcessOrThreadExited(Process* process,
                                     const mx_excp_type_t type,
                                     const mx_exception_context_t& context) {
  // TODO(armansito): Implement
  FTL_LOG(INFO) << (context.tid ? "Thread" : "Process") << " exited";
}

void Server::OnArchitecturalException(Process* process,
                                      Thread* thread,
                                      const mx_excp_type_t type,
                                      const mx_exception_context_t& context) {
  FTL_DCHECK(process);
  FTL_DCHECK(thread);
  FTL_LOG(INFO) << "Architectural Exception";

  // TODO(armansito): Fine-tune this check if we ever support multi-processing.
  FTL_DCHECK(process == current_process());

  int sigval = arch::ComputeGdbSignal(context);
  if (sigval < 0) {
    FTL_LOG(ERROR) << "Exception reporting not supported on current "
                   << "architecture!";
    return;
  }

  FTL_DCHECK(sigval < std::numeric_limits<uint8_t>::max());

  StopReplyPacket stop_reply(StopReplyPacket::Type::kReceivedSignal);
  stop_reply.SetSignalNumber(sigval);
  stop_reply.SetThreadId(process->id(), context.tid);

  // TODO(armansito): The kernel doesn't report MX_EXCP_*_BREAKPOINT currently,
  // so we condition this on MX_EXCP_GENERAL. We always set "swbreak" for the
  // stop reason since that's the only kind we currently support.
  if (type == MX_EXCP_GENERAL)
    stop_reply.SetStopReason("swbreak");

  // Registers.
  if (thread->registers()->RefreshGeneralRegisters()) {
    std::array<int, 3> regnos{{arch::GetFPRegisterNumber(),
                               arch::GetSPRegisterNumber(),
                               arch::GetPCRegisterNumber()}};

    for (int regno : regnos) {
      FTL_DCHECK(regno < std::numeric_limits<uint8_t>::max() && regno >= 0);
      std::string regval = thread->registers()->GetRegisterValue(regno);
      stop_reply.AddRegisterValue(regno, regval);
    }
  } else {
    FTL_LOG(WARNING)
        << "Couldn't read thread registers while handling exception";
  }

  auto packet = stop_reply.Build();
  SendNotification("Stop", ftl::StringView(packet.data(), packet.size()));
}

}  // namespace debugserver
