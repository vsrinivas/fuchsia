// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_channel.h"

#include <endian.h>

#include <magenta/status.h>

#include "lib/ftl/functional/auto_call.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/ftl/time/time_delta.h"

#include "command_packet.h"
#include "transport.h"

namespace bluetooth {
namespace hci {

CommandChannel::QueuedCommand::QueuedCommand(TransactionId id,
                                             common::DynamicByteBuffer command_packet,
                                             const CommandStatusCallback& status_callback,
                                             const CommandCompleteCallback& complete_callback,
                                             ftl::RefPtr<ftl::TaskRunner> task_runner,
                                             const EventCode complete_event_code) {
  packet_data = std::move(command_packet);
  transaction_data.id = id;
  transaction_data.complete_event_code = complete_event_code;
  transaction_data.status_callback = status_callback;
  transaction_data.complete_callback = complete_callback;
  transaction_data.task_runner = task_runner;

  CommandPacket cmd(&packet_data);
  transaction_data.opcode = cmd.opcode();
}

CommandChannel::CommandChannel(Transport* transport, mx::channel hci_command_channel)
    : next_transaction_id_(1u),
      next_event_handler_id_(1u),
      transport_(transport),
      channel_(std::move(hci_command_channel)),
      is_initialized_(false),
      io_handler_key_(0u) {
  FTL_DCHECK(transport_);
  FTL_DCHECK(channel_.is_valid());
}

CommandChannel::~CommandChannel() {
  ShutDown();
}

void CommandChannel::Initialize() {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FTL_DCHECK(!is_initialized_);

  // We make sure that this method blocks until the I/O handler registration task has run.
  std::mutex init_mutex;
  std::condition_variable init_cv;
  bool ready = false;

  io_task_runner_ = transport_->io_task_runner();
  io_task_runner_->PostTask([&] {
    io_handler_key_ =
        mtl::MessageLoop::GetCurrent()->AddHandler(this, channel_.get(), MX_CHANNEL_READABLE);
    FTL_LOG(INFO) << "hci: CommandChannel: I/O handler registered";
    {
      std::lock_guard<std::mutex> lock(init_mutex);
      ready = true;
    }
    init_cv.notify_one();
  });

  std::unique_lock<std::mutex> lock(init_mutex);
  init_cv.wait(lock, [&ready] { return ready; });

  is_initialized_ = true;

  FTL_LOG(INFO) << "hci: CommandChannel: initialized";
}

void CommandChannel::ShutDown() {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  if (!is_initialized_) return;

  FTL_LOG(INFO) << "hci: CommandChannel: shutting down";

  std::mutex init_mutex;
  std::condition_variable init_cv;
  bool ready = false;

  io_task_runner_->PostTask([&, handler_key = io_handler_key_ ] {
    FTL_DCHECK(mtl::MessageLoop::GetCurrent());
    FTL_LOG(INFO) << "hci: CommandChannel: Removing I/O handler";
    SetPendingCommand(nullptr);
    mtl::MessageLoop::GetCurrent()->RemoveHandler(handler_key);

    {
      std::lock_guard<std::mutex> lock(init_mutex);
      ready = true;
    }
    init_cv.notify_one();
  });

  std::unique_lock<std::mutex> lock(init_mutex);
  init_cv.wait(lock, [&ready] { return ready; });

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
    common::DynamicByteBuffer command_packet, const CommandStatusCallback& status_callback,
    const CommandCompleteCallback& complete_callback, ftl::RefPtr<ftl::TaskRunner> task_runner,
    const EventCode complete_event_code) {
  if (!is_initialized_) {
    FTL_VLOG(1) << "hci: CommandChannel: Cannot send commands while uninitialized";
    return 0u;
  }

  std::lock_guard<std::mutex> lock(send_queue_mutex_);

  if (next_transaction_id_ == 0u) next_transaction_id_++;
  TransactionId id = next_transaction_id_++;
  QueuedCommand cmd(id, std::move(command_packet), status_callback, complete_callback, task_runner,
                    complete_event_code);

  send_queue_.push(std::move(cmd));
  io_task_runner_->PostTask(std::bind(&CommandChannel::TrySendNextQueuedCommand, this));

  return id;
}

CommandChannel::EventHandlerId CommandChannel::AddEventHandler(
    EventCode event_code, const EventCallback& event_callback,
    ftl::RefPtr<ftl::TaskRunner> task_runner) {
  FTL_DCHECK(event_code != 0);
  FTL_DCHECK(event_code != kCommandStatusEventCode);
  FTL_DCHECK(event_code != kCommandCompleteEventCode);
  FTL_DCHECK(event_code != kLEMetaEventCode);

  std::lock_guard<std::mutex> lock(event_handler_mutex_);

  if (event_code_handlers_.find(event_code) != event_code_handlers_.end()) {
    FTL_LOG(ERROR) << "hci: event handler already registered for event code: "
                   << ftl::StringPrintf("0x%02x", event_code);
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

  FTL_DCHECK(event_handler_id_map_.find(id) == event_handler_id_map_.end());
  event_handler_id_map_[id] = data;
  event_code_handlers_[event_code] = id;

  return id;
}

CommandChannel::EventHandlerId CommandChannel::AddLEMetaEventHandler(
    EventCode subevent_code, const EventCallback& event_callback,
    ftl::RefPtr<ftl::TaskRunner> task_runner) {
  FTL_DCHECK(subevent_code != 0);

  std::lock_guard<std::mutex> lock(event_handler_mutex_);

  if (subevent_code_handlers_.find(subevent_code) != subevent_code_handlers_.end()) {
    FTL_LOG(ERROR) << "hci: event handler already registered for LE Meta subevent code: "
                   << ftl::StringPrintf("0x%02x", subevent_code);
    return 0u;
  }

  auto id = ++next_event_handler_id_;
  EventHandlerData data;
  data.id = id;
  data.event_code = subevent_code;
  data.event_callback = event_callback;
  data.task_runner = task_runner;
  data.is_le_meta_subevent = true;

  FTL_DCHECK(event_handler_id_map_.find(id) == event_handler_id_map_.end());
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
  FTL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  // If a command is currently pending, then we have nothing to do.
  if (GetPendingCommand()) return;

  QueuedCommand cmd;
  {
    std::lock_guard<std::mutex> lock(send_queue_mutex_);
    if (send_queue_.empty()) return;

    cmd = std::move(send_queue_.front());
    send_queue_.pop();
  }

  mx_status_t status =
      channel_.write(0, cmd.packet_data.data(), cmd.packet_data.size(), nullptr, 0);
  if (status < 0) {
    // TODO(armansito): We should notify the |status_callback| of the pending
    // command with a special error code in this case.
    FTL_LOG(ERROR) << "hci: CommandChannel: Failed to send command: "
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
    FTL_DCHECK(pending_cmd);
    FTL_DCHECK(pending_cmd->id == id);

    pending_cmd->task_runner->PostTask(
        std::bind(pending_cmd->status_callback, id, Status::kCommandTimeout));
    SetPendingCommand(nullptr);
    TrySendNextQueuedCommand();
  });

  io_task_runner_->PostDelayedTask(pending_cmd_timeout_.callback(),
                                   ftl::TimeDelta::FromMilliseconds(kCommandTimeoutMs));
}

void CommandChannel::HandlePendingCommandComplete(const EventPacket& event) {
  FTL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  PendingTransactionData* pending_command = GetPendingCommand();
  FTL_DCHECK(pending_command);
  FTL_DCHECK(event.event_code() == pending_command->complete_event_code);

  // In case that this is a CommandComplete event, make sure that the command
  // opcode actually matches the pending command.
  if (event.event_code() == kCommandCompleteEventCode &&
      le16toh(event.GetPayload<CommandCompleteEventParams>()->command_opcode) !=
          pending_command->opcode) {
    FTL_LOG(ERROR) << ftl::StringPrintf(
        "hci: CommandChannel: Unmatched CommandComplete event - opcode: "
        "0x%04x, pending: 0x%04x",
        le16toh(event.GetPayload<CommandCompleteEventParams>()->command_opcode),
        pending_command->opcode);
    return;
  }

  // In case that this is a CommandComplete event, make sure that the command
  // opcode actually matches the pending command.
  if (event.event_code() == kCommandStatusEventCode &&
      le16toh(event.GetPayload<CommandStatusEventParams>()->command_opcode) !=
          pending_command->opcode) {
    FTL_LOG(ERROR) << ftl::StringPrintf(
        "hci: CommandChannel: Unmatched CommandStatus event - opcode: "
        "0x%04x, pending: 0x%04x",
        le16toh(event.GetPayload<CommandStatusEventParams>()->command_opcode),
        pending_command->opcode);
    return;
  }

  // Clear the pending command and process the next queued command when this goes out of scope.
  auto ac = ftl::MakeAutoCall([this] {
    SetPendingCommand(nullptr);
    TrySendNextQueuedCommand();
  });

  // If the command callback needs to run on the I/O thread, then invoke it immediately.
  // TODO(armansito): Use a slab-allocated ByteBuffer so that we don't need to make a needless
  // copy below.
  if (pending_command->task_runner.get() == io_task_runner_.get()) {
    pending_command->complete_callback(pending_command->id, event);
    return;
  }

  // Use a lambda to capture the copied contents of the buffer. We can't invoke
  // the callback on |event| directly since the backing buffer is owned by this
  // CommandChannel and its contents will be modified.
  common::DynamicByteBuffer buffer(event.size());
  memcpy(buffer.mutable_data(), event.buffer()->data(), event.size());

  pending_command->task_runner->PostTask(ftl::MakeCopyable([
    buffer = std::move(buffer), complete_callback = pending_command->complete_callback,
    transaction_id = pending_command->id
  ]() mutable {
    EventPacket event(&buffer);
    complete_callback(transaction_id, event);
  }));
}

void CommandChannel::HandlePendingCommandStatus(const EventPacket& event) {
  FTL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

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

  auto status_cb =
      std::bind(pending_command->status_callback, pending_command->id,
                static_cast<Status>(event.GetPayload<CommandStatusEventParams>()->status));

  // If the command callback needs to run on the I/O thread, then invoke it immediately.
  if (pending_command->task_runner.get() == io_task_runner_.get()) {
    status_cb();
  } else {
    pending_command->task_runner->PostTask(status_cb);
  }

  // Success in this case means that the command will be completed later when we
  // receive an event that matches |pending_command->complete_event_code|.
  if (event.GetPayload<CommandStatusEventParams>()->status == Status::kSuccess) return;

  // A CommandStatus event with an error status usually means that the command
  // that was in progress could not be executed. Complete the transaction and
  // move on to the next queued command.
  SetPendingCommand(nullptr);
  TrySendNextQueuedCommand();
}

CommandChannel::PendingTransactionData* CommandChannel::GetPendingCommand() {
  FTL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
  return pending_command_.value();
}

void CommandChannel::SetPendingCommand(PendingTransactionData* command) {
  FTL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  // Cancel the pending command timeout handler as the pending command is being reset.
  pending_cmd_timeout_.Cancel();

  if (!command) {
    pending_command_.Reset();
    return;
  }

  FTL_DCHECK(!pending_command_);
  pending_command_ = *command;
}

void CommandChannel::NotifyEventHandler(const EventPacket& event) {
  FTL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  // Ignore HCI_CommandComplete and HCI_CommandStatus events.
  if (event.event_code() == kCommandCompleteEventCode ||
      event.event_code() == kCommandStatusEventCode) {
    FTL_LOG(ERROR) << "hci: Muting unhandled "
                   << (event.event_code() == kCommandCompleteEventCode ? "HCI_CommandComplete"
                                                                       : "HCI_CommandStatus")
                   << " event";
    return;
  }

  std::lock_guard<std::mutex> lock(event_handler_mutex_);

  EventCode event_code;
  const std::unordered_map<EventCode, EventHandlerId>* event_handlers;

  if (event.event_code() == kLEMetaEventCode) {
    event_code = event.GetPayload<LEMetaEventParams>()->subevent_code;
    event_handlers = &subevent_code_handlers_;
  } else {
    event_code = event.event_code();
    event_handlers = &event_code_handlers_;
  }

  auto iter = event_handlers->find(event_code);
  if (iter == event_handlers->end()) {
    // No handler registered for event.
    return;
  }

  auto handler_iter = event_handler_id_map_.find(iter->second);
  FTL_DCHECK(handler_iter != event_handler_id_map_.end());

  auto& handler = handler_iter->second;
  FTL_DCHECK(handler.event_code == event_code);

  // If the given task runner is the I/O task runner, then immediately execute the callback as there
  // is no need to delay the execution.
  // TODO(armansito): Use slab-allocated ByteBuffer so that we don't need to branch and make a
  // needless copy.
  if (handler.task_runner.get() == io_task_runner_.get()) {
    handler.event_callback(event);
  } else {
    common::DynamicByteBuffer buffer(event.size());
    memcpy(buffer.mutable_data(), event.buffer()->data(), event.size());

    handler.task_runner->PostTask(ftl::MakeCopyable(
        [ buffer = std::move(buffer), event_callback = handler.event_callback ]() mutable {
          EventPacket event(&buffer);
          event_callback(event);
        }));
  }
}

void CommandChannel::OnHandleReady(mx_handle_t handle, mx_signals_t pending, uint64_t count) {
  FTL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
  FTL_DCHECK(handle == channel_.get());
  FTL_DCHECK(pending & MX_CHANNEL_READABLE);

  uint32_t read_size;
  mx_status_t status = channel_.read(0u, event_buffer_.mutable_data(), event_buffer_.size(),
                                     &read_size, nullptr, 0, nullptr);
  if (status < 0) {
    FTL_VLOG(1) << "hci: CommandChannel: Failed to read event bytes: "
                << mx_status_get_string(status);
    // Clear the handler so that we stop receiving events from it.
    mtl::MessageLoop::GetCurrent()->RemoveHandler(io_handler_key_);
    return;
  }

  if (read_size < sizeof(EventHeader)) {
    FTL_LOG(ERROR) << "hci: CommandChannel: Malformed event packet - "
                   << "expected at least " << sizeof(EventHeader) << " bytes, "
                   << "got " << read_size;
    // TODO(armansito): Should this be fatal? Ignore for now.
    return;
  }

  const size_t rx_payload_size = read_size - sizeof(EventHeader);
  EventPacket event(&event_buffer_);
  if (event.GetPayloadSize() != rx_payload_size) {
    FTL_LOG(ERROR) << "hci: CommandChannel: Malformed event packet - "
                   << "payload size from header (" << event.GetPayloadSize() << ")"
                   << " does not match received payload size: " << rx_payload_size;
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

  // The event did not match a pending command OR no command is currently
  // pending. Notify the upper layers.
  NotifyEventHandler(event);
}

void CommandChannel::OnHandleError(mx_handle_t handle, mx_status_t error) {
  FTL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
  FTL_DCHECK(handle == channel_.get());

  FTL_VLOG(1) << "hci: CommandChannel: channel error: " << mx_status_get_string(error);

  // Clear the handler so that we stop receiving events from it.
  mtl::MessageLoop::GetCurrent()->RemoveHandler(io_handler_key_);
}

}  // namespace hci
}  // namespace bluetooth
