// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_channel.h"

#include <endian.h>

#include <magenta/status.h>

#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/threading/create_thread.h"

#include "command_packet.h"

namespace bluetooth {
namespace hci {

// static
std::atomic_size_t CommandChannel::next_transaction_id_(0u);

CommandChannel::QueuedCommand::QueuedCommand(
    TransactionId id,
    const CommandPacket& command_packet,
    const CommandStatusCallback& status_callback,
    const CommandCompleteCallback& complete_callback,
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    const EventCode complete_event_code) {
  transaction_data.id = id;
  transaction_data.opcode = command_packet.opcode();
  transaction_data.complete_event_code = complete_event_code;
  transaction_data.status_callback = status_callback;
  transaction_data.complete_callback = complete_callback;
  transaction_data.task_runner = task_runner;

  packet_data = common::DynamicByteBuffer(
      command_packet.size(), command_packet.buffer()->TransferContents());
}

CommandChannel::CommandChannel(mx::channel hci_command_channel)
    : channel_(std::move(hci_command_channel)),
      is_running_(false),
      io_handler_key_(0u),
      is_command_pending_(false) {
  FTL_DCHECK(channel_.get() != MX_HANDLE_INVALID);
}

CommandChannel::~CommandChannel() {
  if (is_running_)
    ShutDown();
}

void CommandChannel::Initialize() {
  FTL_DCHECK(!is_running_);

  is_running_ = true;
  io_thread_ = mtl::CreateThread(&io_task_runner_, "hci-command-channel");

  io_task_runner_->PostTask([this] {
    io_handler_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(
        this, channel_.get(), MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED);
    FTL_LOG(INFO) << "hci: CommandChannel: I/O loop handler registered";
  });

  FTL_LOG(INFO) << "hci: CommandChannel initialized";
}

void CommandChannel::ShutDown() {
  FTL_DCHECK(is_running_);
  FTL_LOG(INFO) << "hci: CommandChannel Shutting down";

  io_task_runner_->PostTask([this] {
    FTL_DCHECK(mtl::MessageLoop::GetCurrent());
    mtl::MessageLoop::GetCurrent()->RemoveHandler(io_handler_key_);
    io_handler_key_ = 0u;

    mtl::MessageLoop::GetCurrent()->QuitNow();
  });

  if (io_thread_.joinable())
    io_thread_.join();

  SetPendingCommand(nullptr);

  send_queue_ = std::queue<QueuedCommand>();
  io_task_runner_ = nullptr;
  is_running_ = false;

  FTL_LOG(INFO) << "hci: CommandChannel: I/O loop exited";
}

CommandChannel::TransactionId CommandChannel::SendCommand(
    const CommandPacket& command_packet,
    const CommandStatusCallback& status_callback,
    const CommandCompleteCallback& complete_callback,
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    const EventCode complete_event_code) {
  // We simply make the counter overflow and do not worry about re-assigning an
  // ID that is currently in use.
  // TODO(armansito): Make this more robust.
  TransactionId id = next_transaction_id_++;
  QueuedCommand cmd(id, command_packet, status_callback, complete_callback,
                    task_runner, complete_event_code);

  std::lock_guard<std::mutex> lock(send_queue_mutex_);
  send_queue_.push(std::move(cmd));
  io_task_runner_->PostTask(
      std::bind(&CommandChannel::TrySendNextQueuedCommand, this));

  return id;
}

void CommandChannel::TrySendNextQueuedCommand() {
  FTL_DCHECK(mtl::MessageLoop::GetCurrent()->task_runner().get() ==
             io_task_runner_.get());

  // If a command is currently pending, then we have nothing to do.
  if (GetPendingCommand())
    return;

  QueuedCommand cmd;
  {
    std::lock_guard<std::mutex> lock(send_queue_mutex_);
    if (send_queue_.empty())
      return;

    cmd = std::move(send_queue_.front());
    send_queue_.pop();
  }

  mx_status_t status = channel_.write(0, cmd.packet_data.GetData(),
                                      cmd.packet_data.GetSize(), nullptr, 0);
  if (status < 0) {
    // TODO(armansito): We should notify the |status_callback| of the pending
    // command with a special error code in this case.
    FTL_LOG(ERROR) << "hci: CommandChannel: Failed to send command: "
                   << mx_status_get_string(status);
    return;
  }

  SetPendingCommand(&cmd.transaction_data);
}

void CommandChannel::HandlePendingCommandComplete(const EventPacket& event) {
  PendingTransactionData* pending_command = GetPendingCommand();
  FTL_DCHECK(pending_command);
  FTL_DCHECK(event.event_code() == pending_command->complete_event_code);

  // In case that this is a CommandComplete event, make sure that the command
  // opcode actually matches the pending command.
  if (event.event_code() == kCommandCompleteEventCode &&
      le16toh(event.GetPayload<CommandCompleteEventParams>()->command_opcode) !=
          pending_command->opcode) {
    FTL_LOG(ERROR) << "hci: CommandChannel: Unmatched CommandComplete event";
    return;
  }

  // Use a lambda to capture the copied contents of the buffer. We can't invoke
  // the callback on |event| directly since the backing buffer is owned by this
  // CommandChannel and its contents will be modified.
  common::DynamicByteBuffer buffer(event.size());
  memcpy(buffer.GetMutableData(), event.buffer()->GetData(), event.size());

  pending_command->task_runner->PostTask(ftl::MakeCopyable([
    buffer = std::move(buffer), payload_size = event.GetPayloadSize(),
    complete_callback = pending_command->complete_callback,
    transaction_id = pending_command->id
  ]() mutable {
    EventPacket event(buffer.GetData()[0], &buffer, payload_size);
    complete_callback(transaction_id, event);
  }));

  SetPendingCommand(nullptr);
  TrySendNextQueuedCommand();
}

void CommandChannel::HandlePendingCommandStatus(const EventPacket& event) {
  PendingTransactionData* pending_command = GetPendingCommand();
  FTL_DCHECK(pending_command);
  FTL_DCHECK(event.event_code() == kCommandStatusEventCode);
  FTL_DCHECK(pending_command->complete_event_code != kCommandStatusEventCode);

  // Make sure that the command opcode actually matches the pending command.
  if (le16toh(event.GetPayload<CommandStatusEventParams>()->command_opcode) !=
      pending_command->opcode) {
    FTL_LOG(ERROR) << "hci: CommandChannel: Unmatched CommandStatus event";
    return;
  }

  pending_command->task_runner->PostTask(
      std::bind(pending_command->status_callback, pending_command->id,
                static_cast<Status>(
                    event.GetPayload<CommandStatusEventParams>()->status)));

  // Success in this case means that the command will be completed later when we
  // receive an event that matches |pending_command->complete_event_code|.
  if (event.GetPayload<CommandStatusEventParams>()->status == Status::kSuccess)
    return;

  // A CommandStatus event with an error status usually means that the command
  // that was in progress could not be executed. Complete the transaction and
  // move on to the next queued command.
  SetPendingCommand(nullptr);
  TrySendNextQueuedCommand();
}

CommandChannel::PendingTransactionData* CommandChannel::GetPendingCommand() {
  if (is_command_pending_)
    return &pending_command_;
  return nullptr;
}

void CommandChannel::SetPendingCommand(PendingTransactionData* command) {
  if (!command) {
    is_command_pending_ = false;
    return;
  }

  FTL_DCHECK(!is_command_pending_);
  pending_command_ = *command;
  is_command_pending_ = true;
}

void CommandChannel::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  FTL_DCHECK(handle == channel_.get());
  FTL_DCHECK(pending & (MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED));

  uint32_t read_size;
  mx_status_t status = channel_.read(0u, event_buffer_.GetMutableData(),
                                     kMaxEventPacketPayloadSize, &read_size,
                                     nullptr, 0, nullptr);
  if (status < 0) {
    FTL_LOG(ERROR) << "hci: CommandChannel: Failed to read event bytes: "
                   << mx_status_get_string(status);
    // TODO(armansito): Notify the upper layers via a callback and unregister
    // the handler.
    return;
  }

  if (read_size < sizeof(EventHeader)) {
    FTL_LOG(ERROR) << "hci: CommandChannel: Malformed event packet - "
                   << "expected at least " << sizeof(EventHeader) << " bytes, "
                   << "got " << read_size;
    // TODO(armansito): Should this be fatal? Ignore for now.
    return;
  }

  EventPacket event(event_buffer_.GetData()[0], &event_buffer_,
                    read_size - sizeof(EventHeader));
  if (event.GetPayloadSize() != event.GetHeader().parameter_total_size) {
    FTL_LOG(ERROR) << "hci: CommandChannel: Malformed event packet - "
                   << "payload size from header ("
                   << event.GetHeader().parameter_total_size << ") "
                   << " does not match received payload size: "
                   << event.GetPayloadSize();
    return;
  }

  // Check to see if this event is in response to the currently pending command.
  PendingTransactionData* pending_command = GetPendingCommand();
  if (pending_command) {
    if (pending_command->complete_event_code == event.event_code()) {
      HandlePendingCommandComplete(event);
      return;
    }

    // For CommandStatus we fall through if the event does not match the
    // currently pending command.
    if (event.event_code() == kCommandStatusEventCode) {
      HandlePendingCommandStatus(event);
      return;
    }
  }

  // TODO(armansito): Don't log anything and notify a registered event
  // handler.
  FTL_LOG(INFO) << "hci: CommandChannel: HCI event: " << event.event_code();
}

void CommandChannel::OnHandleError(mx_handle_t handle, mx_status_t error) {
  FTL_DCHECK(handle == channel_.get());

  FTL_LOG(ERROR) << "hci: CommandChannel: channel error: "
                 << mx_status_get_string(error);

  // TODO(armansito): Notify the upper layers via a callback and unregister the
  // handler.
}

}  // namespace hci
}  // namespace bluetooth
