// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "server.h"

#include <arpa/inet.h>
#include <lib/async/cpp/task.h>
#include <sys/socket.h>

#include <array>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "garnet/lib/debugger_utils/jobs.h"
#include "garnet/lib/debugger_utils/sysinfo.h"
#include "garnet/lib/debugger_utils/util.h"

#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/log_settings.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/strings/string_view.h"

#include "stop_reply_packet.h"
#include "util.h"

namespace debugserver {

namespace {

constexpr char kStopNotification[] = "Stop";
constexpr char kStopAck[] = "vStopped";

}  // namespace

RspServer::PendingNotification::PendingNotification(
    const fxl::StringView& name,
    const fxl::StringView& event,
    const fxl::TimeDelta& timeout)
    : name(name.data(), name.size()),
      event(event.data(), event.size()),
      timeout(timeout) {}

RspServer::RspServer(uint16_t port, zx_koid_t initial_attach_pid)
  : ServerWithIO(util::GetRootJob(), util::GetDefaultJob()),
    port_(port),
    initial_attach_pid_(initial_attach_pid),
    server_sock_(-1),
    command_handler_(this) {
}

bool RspServer::Run() {
  FXL_DCHECK(!io_loop_);

  if (!exception_port_.Run()) {
    FXL_LOG(ERROR) << "Failed to initialize exception port!";
    return false;
  }

  auto cleanup_exception_port = fxl::MakeAutoCall([&]() {
    // Tell the exception port to quit and wait for it to finish.
    FXL_VLOG(2) << "Quitting exception port thread.";
    exception_port_.Quit();
  });

  // If we're to attach to a running process at start-up, do so here.
  // This needs to be done after |exception_port_| is set up.
  if (initial_attach_pid_ != ZX_KOID_INVALID) {
    auto inferior = current_process();
    FXL_DCHECK(!inferior->IsAttached());
    if (!inferior->Attach(initial_attach_pid_)) {
      FXL_LOG(ERROR) << "Failed to attach to inferior";
      return false;
    }
    FXL_DCHECK(inferior->IsAttached());
    // It's Attach()'s job to mark the process as live, since it knows we just
    // attached to an already running program.
    FXL_DCHECK(inferior->IsLive());
  }

  // TODO(dje): Continually re-listen for connections when debugger goes
  // away, with new option to control this (--listen=once|loop or whatever).

  // Listen for an incoming connection.
  if (!Listen())
    return false;

  // |client_sock_| should be ready to be consumed now.
  FXL_DCHECK(client_sock_.is_valid());

  io_loop_ = std::make_unique<RspIOLoop>(client_sock_.get(), this,
                                         &message_loop_);
  io_loop_->Run();

  // Start the main loop.
  message_loop_.Run();

  FXL_LOG(INFO) << "Main loop exited";

  // Tell the I/O loop to quit its message loop and wait for it to finish.
  io_loop_->Quit();

  return run_status_;
}

void RspServer::QueueNotification(const fxl::StringView& name,
                                  const fxl::StringView& event,
                                  const fxl::TimeDelta& timeout) {
  // The GDB Remote protocol defines only the "Stop" notification
  FXL_DCHECK(name == kStopNotification);

  FXL_VLOG(1) << "Preparing notification: " << name << ":" << event;

  notify_queue_.push(
      std::make_unique<PendingNotification>(name, event, timeout));
  TryPostNextNotification();
}

void RspServer::QueueStopNotification(const fxl::StringView& event,
                                      const fxl::TimeDelta& timeout) {
  QueueNotification(kStopNotification, event, timeout);
}

bool RspServer::SetParameter(const fxl::StringView& parameter,
                             const fxl::StringView& value) {
  if (parameter == "verbosity") {
    int verbosity;
    if (!fxl::StringToNumberWithError<int>(value, &verbosity)) {
      FXL_LOG(ERROR) << "Malformed verbosity level: " << value;
      return false;
    }
    // We only support verbosity levels up to 2 (recorded as -2) but there's
    // no point in disallowing higher levels (larger negative values). OTOH,
    // we do want to catch bad severity levels (positive values).
    if (verbosity >= static_cast<int>(fxl::LOG_NUM_SEVERITIES)) {
      FXL_LOG(ERROR) << "Invalid verbosity level: " << value;
      return false;
    }
    fxl::LogSettings log_settings = fxl::GetLogSettings();
    log_settings.min_log_level = static_cast<fxl::LogSeverity>(verbosity);
    fxl::SetLogSettings(log_settings);
    return true;
  } else {
    FXL_LOG(ERROR) << "Invalid parameter: " << parameter;
    return false;
  }
}

bool RspServer::GetParameter(const fxl::StringView& parameter,
                             std::string* value) {
  if (parameter == "verbosity") {
    *value = fxl::NumberToString<int>(fxl::GetMinLogLevel());
    return true;
  } else {
    FXL_LOG(ERROR) << "Invalid parameter: " << parameter;
    return false;
  }
}

bool RspServer::Listen() {
  FXL_DCHECK(!server_sock_.is_valid());
  FXL_DCHECK(!client_sock_.is_valid());

  fxl::UniqueFD server_sock(socket(AF_INET, SOCK_STREAM, 0));
  if (!server_sock.is_valid()) {
    FXL_LOG(ERROR) << "Failed to open socket"
                   << ", " << util::ErrnoString(errno);
    return false;
  }

  // Bind to a local address for listening.
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);

  if (bind(server_sock.get(), (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    FXL_LOG(ERROR) << "Failed to bind socket"
                   << ", " << util::ErrnoString(errno);
    return false;
  }

  if (listen(server_sock.get(), 1) < 0) {
    FXL_LOG(ERROR) << "Listen failed"
                   << ", " << util::ErrnoString(errno);
    return false;
  }

  FXL_LOG(INFO) << "Waiting for a connection on port " << port_ << "...";

  // Reuse |addr| here for the destination address.
  socklen_t addrlen = sizeof(addr);
  fxl::UniqueFD client_sock(
      accept(server_sock.get(), (struct sockaddr*)&addr, &addrlen));
  if (!client_sock.is_valid()) {
    FXL_LOG(ERROR) << "Accept failed"
                   << ", " << util::ErrnoString(errno);
    return false;
  }

  FXL_LOG(INFO) << "Client connected";

  server_sock_ = std::move(server_sock);
  client_sock_ = std::move(client_sock);

  return true;
}

void RspServer::SendAck(bool ack) {
  // TODO(armansito): Don't send anything if we're in no-acknowledgment mode. We
  // currently don't support this mode.
  FXL_DCHECK(io_loop_);
  char payload = ack ? '+' : '-';
  io_loop_->PostWriteTask(fxl::StringView(&payload, 1));
}

void RspServer::PostWriteTask(bool notify, const fxl::StringView& data) {
  FXL_DCHECK(io_loop_);
  FXL_DCHECK(data.size() + 4 < kMaxBufferSize);

  // Copy the data into a std::string to capture it in the closure.
  async::PostTask(
      message_loop_.dispatcher(), [this, data = data.ToString(), notify] {
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

        io_loop_->PostWriteTask(fxl::StringView(out_buffer_.data(), index));
      });
}

void RspServer::PostPacketWriteTask(const fxl::StringView& data) {
  PostWriteTask(false, data);
}

void RspServer::PostPendingNotificationWriteTask() {
  FXL_DCHECK(pending_notification_);
  std::string bytes(pending_notification_->name + ":" +
                    pending_notification_->event);
  PostWriteTask(true, bytes);
}

bool RspServer::TryPostNextNotification() {
  if (pending_notification_ || notify_queue_.empty())
    return false;

  pending_notification_ = std::move(notify_queue_.front());
  notify_queue_.pop();
  FXL_DCHECK(pending_notification_);

  // Send the notification.
  PostPendingNotificationWriteTask();
  PostNotificationTimeoutHandler();
  return true;
}

void RspServer::PostNotificationTimeoutHandler() {
  // Set up a timeout handler.
  // Continually resend the notification until the remote end acknowledges it,
  // or until the notification is removed (say because the process exits).
  zx::duration delay =
      zx::nsec((pending_notification_->timeout).ToNanoseconds());
  async::PostDelayedTask(message_loop_.dispatcher(),
                         [this, pending = pending_notification_.get()] {
                           // If the notification that we set this timeout for
                           // has already been acknowledged by the remote, then
                           // we have nothing to do.
                           // TODO(dje): sequence numbers?
                           if (pending_notification_.get() != pending)
                             return;

                           FXL_LOG(WARNING)
                               << "Notification timed out; retrying";
                           PostPendingNotificationWriteTask();
                           PostNotificationTimeoutHandler();
                         },
                         delay);
}

void RspServer::OnBytesRead(const fxl::StringView& bytes_read) {
  // If this is a packet acknowledgment then ignore it and read again.
  // TODO(armansito): Re-send previous packet if we got "-".
  if (bytes_read == "+")
    return;

  fxl::StringView packet_data;
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
      FXL_VLOG(2) << "Notification acknowledged";

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
      FXL_VLOG(2) << "Notification acknowledged, but notification gone";
    }
    return;
  }

  // Route the packet data to the command handler.
  auto callback = [this](const fxl::StringView& rsp) {
    // Send the response if there is one.
    PostPacketWriteTask(rsp);
  };

  // If the command is handled, then |callback| will be called at some point,
  // so we're done.
  if (command_handler_.HandleCommand(packet_data, callback))
    return;

  // If the command wasn't handled, that's because we do not support it, so we
  // respond with an empty response and continue.
  FXL_LOG(ERROR) << "Command not supported: " << packet_data;
  callback("");
}

void RspServer::OnDisconnected() {
  // Exit successfully in the case of a remote disconnect.
  FXL_LOG(INFO) << "Client disconnected";
  QuitMessageLoop(true);
}

void RspServer::OnIOError() {
  FXL_LOG(ERROR) << "An I/O error has occurred. Exiting the main loop";
  QuitMessageLoop(false);
}

void RspServer::OnThreadStarting(Process* process,
                                 Thread* thread,
                                 const zx_exception_context_t& context) {
  FXL_DCHECK(process);

  // TODO(armansito): We send a stop-reply packet for the new thread. This
  // inherently completes any pending vRun sequence but technically shouldn't be
  // sent unless GDB enables QThreadEvents. Add some logic here to send this
  // conditionally only when necessary.
  StopReplyPacket stop_reply(StopReplyPacket::Type::kReceivedSignal);
  stop_reply.SetSignalNumber(5);
  stop_reply.SetThreadId(process->id(), thread->id());
  stop_reply.SetStopReason("create");

  auto packet = stop_reply.Build();

  switch (process->state()) {
    case Process::State::kStarting:
      // vRun receives a synchronous response. After that it's all asynchronous.
      PostPacketWriteTask(fxl::StringView(packet.data(), packet.size()));
      process->set_state(Process::State::kRunning);
      break;
    case Process::State::kRunning:
      QueueStopNotification(fxl::StringView(packet.data(), packet.size()));
      break;
    default:
      FXL_DCHECK(false);
  }
}

void RspServer::OnThreadExiting(Process* process,
                                Thread* thread,
                                const zx_exception_context_t& context) {
  std::vector<char> packet;
  FXL_LOG(INFO) << "Thread " << thread->GetName() << " exited";
  int exit_code = 0;  // TODO(dje)
  StopReplyPacket stop_reply(StopReplyPacket::Type::kThreadExited);
  stop_reply.SetSignalNumber(exit_code);
  stop_reply.SetThreadId(process->id(), thread->id());
  packet = stop_reply.Build();
  QueueStopNotification(fxl::StringView(packet.data(), packet.size()));

  // The Remote Serial Protocol doesn't provide for a means to examine
  // state when exiting, like it does when starting. The thread needs to be
  // "resumed" so that the o/s will finish terminating the thread. This also
  // takes care of marking the thread as kGone.
  thread->ResumeForExit();
}

void RspServer::OnProcessExit(Process* process) {
  std::vector<char> packet;
  FXL_LOG(INFO) << "Process " << process->GetName() << " exited";
  SetCurrentThread(nullptr);
  int exit_code = process->ExitCode();
  StopReplyPacket stop_reply(StopReplyPacket::Type::kProcessExited);
  stop_reply.SetSignalNumber(exit_code);
  packet = stop_reply.Build();
  QueueStopNotification(fxl::StringView(packet.data(), packet.size()));
}

void RspServer::OnArchitecturalException(
    Process* process,
    Thread* thread,
    const zx_excp_type_t type,
    const zx_exception_context_t& context) {
  ExceptionHelper(process, thread, type, context);
}

void RspServer::OnSyntheticException(Process* process,
                                     Thread* thread,
                                     zx_excp_type_t type,
                                     const zx_exception_context_t& context) {
  // These are basically equivalent to architectural exceptions
  // for our purposes. Handle them the same way.
  ExceptionHelper(process, thread, type, context);
}

void RspServer::ExceptionHelper(Process* process,
                                Thread* thread,
                                zx_excp_type_t type,
                                const zx_exception_context_t& context) {
  FXL_DCHECK(process);
  FXL_DCHECK(thread);

  if (ZX_EXCP_IS_ARCH(type)) {
    FXL_VLOG(1) << "Architectural Exception: "
                << util::ExceptionToString(type, context);
  } else {
    FXL_VLOG(1) << "Synthetic Exception: "
                << util::ExceptionToString(type, context);
  }

  // TODO(armansito): Fine-tune this check if we ever support multi-processing.
  FXL_DCHECK(process == current_process());

  arch::GdbSignal sigval = thread->GetGdbSignal();
  if (sigval == arch::GdbSignal::kUnsupported) {
    FXL_LOG(ERROR) << "Exception reporting not supported on current "
                   << "architecture!";
    return;
  }
  int isigval = static_cast<int>(sigval);

  FXL_DCHECK(isigval < std::numeric_limits<uint8_t>::max());

  StopReplyPacket stop_reply(StopReplyPacket::Type::kReceivedSignal);
  stop_reply.SetSignalNumber(isigval);
  stop_reply.SetThreadId(process->id(), thread->id());

  // Registers.
  if (thread->registers()->RefreshGeneralRegisters()) {
    std::array<int, 3> regnos{{arch::GetFPRegisterNumber(),
                               arch::GetSPRegisterNumber(),
                               arch::GetPCRegisterNumber()}};
    for (int regno : regnos) {
      FXL_DCHECK(regno < std::numeric_limits<uint8_t>::max() && regno >= 0);
      std::string regval = thread->registers()->GetRegisterAsString(regno);
      stop_reply.AddRegisterValue(regno, regval);
    }
  } else {
    FXL_LOG(WARNING)
        << "Couldn't read thread registers while handling exception";
  }

  auto packet = stop_reply.Build();
  QueueStopNotification(fxl::StringView(packet.data(), packet.size()));
}

}  // namespace debugserver
