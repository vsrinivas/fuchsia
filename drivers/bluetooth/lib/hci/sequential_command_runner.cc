// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sequential_command_runner.h"

#include "command_channel.h"
#include "hci.h"
#include "transport.h"

namespace bluetooth {
namespace hci {

SequentialCommandRunner::SequentialCommandRunner(ftl::RefPtr<ftl::TaskRunner> task_runner,
                                                 ftl::RefPtr<Transport> transport)
    : task_runner_(task_runner), transport_(transport), weak_ptr_factory_(this) {
  FTL_DCHECK(task_runner_);
  FTL_DCHECK(transport_);
  FTL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
}

SequentialCommandRunner::~SequentialCommandRunner() {
  FTL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
}

void SequentialCommandRunner::QueueCommand(common::DynamicByteBuffer command_packet,
                                           const CommandCompleteCallback& callback) {
  FTL_DCHECK(!result_callback_);
  FTL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  FTL_DCHECK(sizeof(CommandHeader) <= command_packet.GetSize());

  command_queue_.push(std::make_pair(std::move(command_packet), callback));
}

void SequentialCommandRunner::RunCommands(const ResultCallback& result_callback) {
  FTL_DCHECK(!result_callback_);
  FTL_DCHECK(result_callback);
  FTL_DCHECK(!command_queue_.empty());
  FTL_DCHECK(task_runner_->RunsTasksOnCurrentThread());

  result_callback_ = result_callback;

  RunNextQueuedCommand();
}

bool SequentialCommandRunner::IsReady() const {
  FTL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  return !result_callback_;
}

bool SequentialCommandRunner::HasQueuedCommands() const {
  FTL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  return !command_queue_.empty();
}

void SequentialCommandRunner::RunNextQueuedCommand() {
  if (command_queue_.empty()) {
    NotifyResultAndReset(true);
    return;
  }

  auto next = std::move(command_queue_.front());
  command_queue_.pop();

  auto status_cb = [ result_cb = result_callback_, self = weak_ptr_factory_.GetWeakPtr() ](
      CommandChannel::TransactionId, Status status) {
    if (!self) {
      result_cb(false);
      return;
    }
    if (status != Status::kSuccess) self->NotifyResultAndReset(false);
  };

  auto complete_cb = [
    cmd_cb = next.second, result_cb = result_callback_, self = weak_ptr_factory_.GetWeakPtr()
  ](CommandChannel::TransactionId, const EventPacket& event_packet) {
    // If |self| was invalidated then terminate the entire sequence.
    if (!self) {
      result_cb(false);
      return;
    }

    auto status = event_packet.GetReturnParams<SimpleReturnParams>()->status;
    if (status != Status::kSuccess) {
      self->NotifyResultAndReset(false);
      return;
    }

    if (cmd_cb) cmd_cb(event_packet);

    self->RunNextQueuedCommand();
  };

  if (!transport_->command_channel()->SendCommand(std::move(next.first), status_cb, complete_cb,
                                                  task_runner_)) {
    NotifyResultAndReset(false);
  }
}

void SequentialCommandRunner::NotifyResultAndReset(bool result) {
  FTL_DCHECK(result_callback_);
  if (!command_queue_.empty()) command_queue_ = {};

  // Reset |result_callback| before invoking it so that the callback implementation can immediately
  // reuse this SequentialCommandRunner.
  auto result_cb = result_callback_;
  result_callback_ = nullptr;
  result_cb(result);
}

}  // namespace hci
}  // namespace bluetooth
