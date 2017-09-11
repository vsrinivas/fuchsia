// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_channel.h"

#include <endian.h>

#include <magenta/status.h>

#include "apps/bluetooth/lib/common/run_task_sync.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/time/time_delta.h"

#include "slab_allocators.h"
#include "transport.h"

namespace bluetooth {
namespace hci {

CommandChannel::QueuedCommand::QueuedCommand(TransactionId id,
                                             std::unique_ptr<CommandPacket> command_packet,
                                             const CommandStatusCallback& status_callback,
                                             const CommandCompleteCallback& complete_callback,
                                             fxl::RefPtr<fxl::TaskRunner> task_runner,
                                             const EventCode complete_event_code,
                                             const EventMatcher& complete_event_matcher) {
  transaction_data.id = id;
  transaction_data.opcode = command_packet->opcode();
  transaction_data.complete_event_code = complete_event_code;
  transaction_data.complete_event_matcher = complete_event_matcher;
  transaction_data.status_callback = status_callback;
  transaction_data.complete_callback = complete_callback;
  transaction_data.task_runner = task_runner;

  packet = std::move(command_packet);
}

CommandChannel::CommandChannel(Transport* transport, mx::channel hci_command_channel)
    : next_transaction_id_(1u),
      next_event_handler_id_(1u),
      transport_(transport),
      channel_(std::move(hci_command_channel)),
      is_initialized_(false),
      io_handler_key_(0u) {
  FXL_DCHECK(transport_);
  FXL_DCHECK(channel_.is_valid());
}

CommandChannel::~CommandChannel() {
  ShutDown();
}

void CommandChannel::Initialize() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(!is_initialized_);

  auto setup_handler_task = [this] {
    io_handler_key_ =
        mtl::MessageLoop::GetCurrent()->AddHandler(this, channel_.get(), MX_CHANNEL_READABLE);
    FXL_LOG(INFO) << "hci: CommandChannel: I/O handler registered";
  };

  io_task_runner_ = transport_->io_task_runner();
  common::RunTaskSync(setup_handler_task, io_task_runner_);

  is_initialized_ = true;

  FXL_LOG(INFO) << "hci: CommandChannel: initialized";
}

void CommandChannel::ShutDown() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  if (!is_initialized_) return;

  FXL_LOG(INFO) << "hci: CommandChannel: shutting down";

  auto handler_cleanup_task = [this] {
    FXL_DCHECK(mtl::MessageLoop::GetCurrent());
    FXL_LOG(INFO) << "hci: CommandChannel: Removing I/O handler";
    SetPendingCommand(nullptr);
    mtl::MessageLoop::GetCurrent()->RemoveHandler(io_handler_key_);
  };

  common::RunTaskSync(handler_cleanup_task, io_task_runner_);

  is_initialized_ = false;

  {
    std::lock_guard<std::mutex> lock(send_queue_mutex_);
    send_queue_ = std::queue<QueuedCommand>();
  }
  {
    std::lock_guard<std::mutex> lock(event_handler_mutex_);
    event_handler_id_map_.clear();
    event_code_handlers_.clear();
    subevent_code_handlers_.clear();
  }
  io_task_runner_ = nullptr;
  io_handler_key_ = 0u;
}

CommandChannel::TransactionId CommandChannel::SendCommand(
    std::unique_ptr<CommandPacket> command_packet, fxl::RefPtr<fxl::TaskRunner> task_runner,
    const CommandCompleteCallback& complete_callback, const CommandStatusCallback& status_callback,
    const EventCode complete_event_code, const EventMatcher& complete_event_matcher) {
  if (!is_initialized_) {
    FXL_VLOG(1) << "hci: CommandChannel: Cannot send commands while uninitialized";
    return 0u;
  }

  FXL_DCHECK(command_packet);

  std::lock_guard<std::mutex> lock(send_queue_mutex_);

  if (next_transaction_id_ == 0u) next_transaction_id_++;
  TransactionId id = next_transaction_id_++;
  send_queue_.push(QueuedCommand(id, std::move(command_packet), status_callback, complete_callback,
                                 task_runner, complete_event_code, complete_event_matcher));
  io_task_runner_->PostTask(std::bind(&CommandChannel::TrySendNextQueuedCommand, this));

  return id;
}

CommandChannel::EventHandlerId CommandChannel::AddEventHandler(
    EventCode event_code, const EventCallback& event_callback,
    fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(event_code != 0);
  FXL_DCHECK(event_code != kCommandStatusEventCode);
  FXL_DCHECK(event_code != kCommandCompleteEventCode);
  FXL_DCHECK(event_code != kLEMetaEventCode);

  std::lock_guard<std::mutex> lock(event_handler_mutex_);

  if (event_code_handlers_.find(event_code) != event_code_handlers_.end()) {
    FXL_LOG(ERROR) << "hci: event handler already registered for event code: "
                   << fxl::StringPrintf("0x%02x", event_code);
    return 0u;
  }

  if (next_event_handler_id_ == 0u) next_event_handler_id_++;
  auto id = next_event_handler_id_++;
  EventHandlerData data;
  data.id = id;
  data.event_code = event_code;
  data.event_callback = event_callback;
  data.task_runner = task_runner;
  data.is_le_meta_subevent = false;

  FXL_DCHECK(event_handler_id_map_.find(id) == event_handler_id_map_.end());
  event_handler_id_map_[id] = data;
  event_code_handlers_[event_code] = id;

  return id;
}

CommandChannel::EventHandlerId CommandChannel::AddLEMetaEventHandler(
    EventCode subevent_code, const EventCallback& event_callback,
    fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(subevent_code != 0);

  std::lock_guard<std::mutex> lock(event_handler_mutex_);

  if (subevent_code_handlers_.find(subevent_code) != subevent_code_handlers_.end()) {
    FXL_LOG(ERROR) << "hci: event handler already registered for LE Meta subevent code: "
                   << fxl::StringPrintf("0x%02x", subevent_code);
    return 0u;
  }

  auto id = ++next_event_handler_id_;
  EventHandlerData data;
  data.id = id;
  data.event_code = subevent_code;
  data.event_callback = event_callback;
  data.task_runner = task_runner;
  data.is_le_meta_subevent = true;

  FXL_DCHECK(event_handler_id_map_.find(id) == event_handler_id_map_.end());
  event_handler_id_map_[id] = data;
  subevent_code_handlers_[subevent_code] = id;

  return id;
}

void CommandChannel::RemoveEventHandler(const EventHandlerId id) {
  std::lock_guard<std::mutex> lock(event_handler_mutex_);

  auto iter = event_handler_id_map_.find(id);
  if (iter == event_handler_id_map_.end()) return;

  if (iter->second.event_code != 0) {
    if (iter->second.is_le_meta_subevent) {
      subevent_code_handlers_.erase(iter->second.event_code);
    } else {
      event_code_handlers_.erase(iter->second.event_code);
    }
  }
  event_handler_id_map_.erase(iter);
}

void CommandChannel::TrySendNextQueuedCommand() {
  if (!is_initialized_) return;
  FXL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  // If a command is currently pending, then we have nothing to do.
  if (GetPendingCommand()) return;

  QueuedCommand cmd;
  {
    std::lock_guard<std::mutex> lock(send_queue_mutex_);
    if (send_queue_.empty()) return;

    cmd = std::move(send_queue_.front());
    send_queue_.pop();
  }

  auto packet_bytes = cmd.packet->view().data();
  mx_status_t status = channel_.write(0, packet_bytes.data(), packet_bytes.size(), nullptr, 0);
  if (status < 0) {
    // TODO(armansito): We should notify the |status_callback| of the pending
    // command with a special error code in this case.
    FXL_LOG(ERROR) << "hci: CommandChannel: Failed to send command: "
                   << mx_status_get_string(status);
    return;
  }

  SetPendingCommand(&cmd.transaction_data);

  // Set a callback to execute if this HCI command times out (i.e the controller does not send back
  // a response in time). Once the command is completed (due to HCI_Command_Status or the
  // corresponding completion callback) this timeout callback will be cancelled when
  // SetPendingCommand() is called to clear the pending command.
  pending_cmd_timeout_.Reset([ this, id = cmd.transaction_data.id ] {
    auto pending_cmd = GetPendingCommand();

    // If this callback is ever invoked then the command that timed out should still be pending.
    FXL_DCHECK(pending_cmd);
    FXL_DCHECK(pending_cmd->id == id);

    if (pending_cmd->status_callback) {
      pending_cmd->task_runner->PostTask(
          std::bind(pending_cmd->status_callback, id, Status::kCommandTimeout));
    }
    SetPendingCommand(nullptr);
    TrySendNextQueuedCommand();
  });

  io_task_runner_->PostDelayedTask(pending_cmd_timeout_.callback(),
                                   fxl::TimeDelta::FromMilliseconds(kCommandTimeoutMs));
}

bool CommandChannel::HandlePendingCommandComplete(std::unique_ptr<EventPacket>&& event) {
  FXL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  PendingTransactionData* pending_command = GetPendingCommand();
  FXL_DCHECK(pending_command);
  FXL_DCHECK(event->event_code() == pending_command->complete_event_code);

  // In case that this is a CommandComplete event, make sure that the command
  // opcode actually matches the pending command.
  if (event->event_code() == kCommandCompleteEventCode &&
      le16toh(event->view().payload<CommandCompleteEventParams>().command_opcode) !=
          pending_command->opcode) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "hci: CommandChannel: Unmatched CommandComplete event - opcode: "
        "0x%04x, pending: 0x%04x",
        le16toh(event->view().payload<CommandCompleteEventParams>().command_opcode),
        pending_command->opcode);
    return false;
  }

  // In case that this is a CommandComplete event, make sure that the command
  // opcode actually matches the pending command.
  if (event->event_code() == kCommandStatusEventCode &&
      le16toh(event->view().payload<CommandStatusEventParams>().command_opcode) !=
          pending_command->opcode) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "hci: CommandChannel: Unmatched CommandStatus event - opcode: "
        "0x%04x, pending: 0x%04x",
        le16toh(event->view().payload<CommandStatusEventParams>().command_opcode),
        pending_command->opcode);
    return false;
  }

  // Do not handle the event if it does not pass the matcher (if one was provided).
  if (pending_command->complete_event_matcher && !pending_command->complete_event_matcher(*event)) {
    return false;
  }

  // Clear the pending command and process the next queued command when this goes out of scope.
  auto ac = fxl::MakeAutoCall([this] {
    SetPendingCommand(nullptr);
    TrySendNextQueuedCommand();
  });

  // If no command complete callback was provided, then the caller does not care about the result.
  if (!pending_command->complete_callback) return true;

  // If the command callback needs to run on the I/O thread (i.e. the current thread), then no need
  // for an async task; invoke the callback immediately.
  if (pending_command->task_runner->RunsTasksOnCurrentThread()) {
    pending_command->complete_callback(pending_command->id, *event);
    return true;
  }

  pending_command->task_runner->PostTask(fxl::MakeCopyable([
    event = std::move(event), complete_callback = pending_command->complete_callback,
    transaction_id = pending_command->id
  ]() mutable { complete_callback(transaction_id, *event); }));

  return true;
}

void CommandChannel::HandlePendingCommandStatus(const EventPacket& event) {
  FXL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  PendingTransactionData* pending_command = GetPendingCommand();
  FXL_DCHECK(pending_command);
  FXL_DCHECK(event.event_code() == kCommandStatusEventCode);
  FXL_DCHECK(pending_command->complete_event_code != kCommandStatusEventCode);

  // Make sure that the command opcode actually matches the pending command.
  if (le16toh(event.view().payload<CommandStatusEventParams>().command_opcode) !=
      pending_command->opcode) {
    FXL_LOG(ERROR) << "hci: CommandChannel: Unmatched CommandStatus event";
    return;
  }

  if (pending_command->status_callback) {
    auto status_cb =
        std::bind(pending_command->status_callback, pending_command->id,
                  static_cast<Status>(event.view().payload<CommandStatusEventParams>().status));

    // If the command callback needs to run on the I/O thread, then invoke it immediately.
    if (pending_command->task_runner->RunsTasksOnCurrentThread()) {
      status_cb();
    } else {
      pending_command->task_runner->PostTask(status_cb);
    }
  }

  // Success in this case means that the command will be completed later when we
  // receive an event that matches |pending_command->complete_event_code|.
  if (event.view().payload<CommandStatusEventParams>().status == Status::kSuccess) return;

  // A CommandStatus event with an error status usually means that the command
  // that was in progress could not be executed. Complete the transaction and
  // move on to the next queued command.
  SetPendingCommand(nullptr);
  TrySendNextQueuedCommand();
}

CommandChannel::PendingTransactionData* CommandChannel::GetPendingCommand() {
  FXL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
  return pending_command_.value();
}

void CommandChannel::SetPendingCommand(PendingTransactionData* command) {
  FXL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  // Cancel the pending command timeout handler as the pending command is being reset.
  pending_cmd_timeout_.Cancel();

  if (!command) {
    pending_command_.Reset();
    return;
  }

  FXL_DCHECK(!pending_command_);
  pending_command_ = *command;
}

void CommandChannel::NotifyEventHandler(std::unique_ptr<EventPacket> event) {
  FXL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  // Ignore HCI_CommandComplete and HCI_CommandStatus events.
  if (event->event_code() == kCommandCompleteEventCode ||
      event->event_code() == kCommandStatusEventCode) {
    FXL_LOG(ERROR) << "hci: Ignoring unhandled "
                   << (event->event_code() == kCommandCompleteEventCode ? "HCI_CommandComplete"
                                                                        : "HCI_CommandStatus")
                   << " event";
    return;
  }

  std::lock_guard<std::mutex> lock(event_handler_mutex_);

  EventCode event_code;
  const std::unordered_map<EventCode, EventHandlerId>* event_handlers;

  if (event->event_code() == kLEMetaEventCode) {
    event_code = event->view().payload<LEMetaEventParams>().subevent_code;
    event_handlers = &subevent_code_handlers_;
  } else {
    event_code = event->event_code();
    event_handlers = &event_code_handlers_;
  }

  auto iter = event_handlers->find(event_code);
  if (iter == event_handlers->end()) {
    // No handler registered for event.
    return;
  }

  auto handler_iter = event_handler_id_map_.find(iter->second);
  FXL_DCHECK(handler_iter != event_handler_id_map_.end());

  auto& handler = handler_iter->second;
  FXL_DCHECK(handler.event_code == event_code);

  // If the given task runner is the I/O task runner, then immediately execute the callback as there
  // is no need to delay the execution.
  if (handler.task_runner.get() == io_task_runner_.get()) {
    handler.event_callback(*event);
    return;
  }

  // Post the event on the requested task runner.
  handler.task_runner->PostTask(fxl::MakeCopyable([
    event = std::move(event), event_callback = handler.event_callback
  ]() mutable { event_callback(*event); }));
}

void CommandChannel::OnHandleReady(mx_handle_t handle, mx_signals_t pending, uint64_t count) {
  FXL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
  FXL_DCHECK(handle == channel_.get());
  FXL_DCHECK(pending & MX_CHANNEL_READABLE);

  // Allocate a buffer for the event. Since we don't know the size beforehand we allocate the
  // largest possible buffer.
  // TODO(armansito): We could first try to read into a small buffer and retry if the syscall
  // returns MX_ERR_BUFFER_TOO_SMALL. Not sure if the second syscall would be worth it but
  // investigate.

  auto packet = EventPacket::New(slab_allocators::kLargeControlPayloadSize);
  if (!packet) {
    FXL_LOG(ERROR) << "Failed to allocate event packet!";
    return;
  }

  uint32_t read_size;
  auto packet_bytes = packet->mutable_view()->mutable_data();
  mx_status_t status = channel_.read(0u, packet_bytes.mutable_data(), packet_bytes.size(),
                                     &read_size, nullptr, 0, nullptr);
  if (status < 0) {
    FXL_VLOG(1) << "hci: CommandChannel: Failed to read event bytes: "
                << mx_status_get_string(status);
    // Clear the handler so that we stop receiving events from it.
    mtl::MessageLoop::GetCurrent()->RemoveHandler(io_handler_key_);
    return;
  }

  if (read_size < sizeof(EventHeader)) {
    FXL_LOG(ERROR) << "hci: CommandChannel: Malformed event packet - "
                   << "expected at least " << sizeof(EventHeader) << " bytes, "
                   << "got " << read_size;
    // TODO(armansito): Should this be fatal? Ignore for now.
    return;
  }

  // Compare the received payload size to what is in the header.
  const size_t rx_payload_size = read_size - sizeof(EventHeader);
  const size_t size_from_header = packet->view().header().parameter_total_size;
  if (size_from_header != rx_payload_size) {
    FXL_LOG(ERROR) << "hci: CommandChannel: Malformed event packet - "
                   << "payload size from header (" << size_from_header << ")"
                   << " does not match received payload size: " << rx_payload_size;
    return;
  }

  packet->InitializeFromBuffer();

  // Check to see if this event is in response to the currently pending command.
  PendingTransactionData* pending_command = GetPendingCommand();
  if (pending_command) {
    if (pending_command->complete_event_code == packet->event_code()) {
      if (HandlePendingCommandComplete(std::move(packet))) return;

      // |packet| should not have been moved in this case. It will be accessed below.
      FXL_DCHECK(packet);
    } else if (packet->event_code() == kCommandStatusEventCode) {
      HandlePendingCommandStatus(*packet);
      return;
    }

    // Fall through if the event did not match the currently pending command.
  }

  // The event did not match a pending command OR no command is currently
  // pending. Notify the upper layers.
  NotifyEventHandler(std::move(packet));
}

void CommandChannel::OnHandleError(mx_handle_t handle, mx_status_t error) {
  FXL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
  FXL_DCHECK(handle == channel_.get());

  FXL_VLOG(1) << "hci: CommandChannel: channel error: " << mx_status_get_string(error);

  // Clear the handler so that we stop receiving events from it.
  mtl::MessageLoop::GetCurrent()->RemoveHandler(io_handler_key_);
}

}  // namespace hci
}  // namespace bluetooth
