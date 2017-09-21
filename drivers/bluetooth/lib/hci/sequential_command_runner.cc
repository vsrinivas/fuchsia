// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sequential_command_runner.h"

#include "command_channel.h"
#include "hci.h"
#include "transport.h"

namespace bluetooth {
namespace hci {

SequentialCommandRunner::SequentialCommandRunner(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    fxl::RefPtr<Transport> transport)
    : task_runner_(task_runner), transport_(transport), sequence_number_(0u) {
  FXL_DCHECK(task_runner_);
  FXL_DCHECK(transport_);
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
}

SequentialCommandRunner::~SequentialCommandRunner() {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
}

void SequentialCommandRunner::QueueCommand(
    std::unique_ptr<CommandPacket> command_packet,
    const CommandCompleteCallback& callback) {
  FXL_DCHECK(!result_callback_);
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  FXL_DCHECK(sizeof(CommandHeader) <= command_packet->view().size());

  command_queue_.push(std::make_pair(std::move(command_packet), callback));
}

void SequentialCommandRunner::RunCommands(
    const ResultCallback& result_callback) {
  FXL_DCHECK(!result_callback_);
  FXL_DCHECK(result_callback);
  FXL_DCHECK(!command_queue_.empty());
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());

  result_callback_ = result_callback;
  sequence_number_++;

  RunNextQueuedCommand();
}

bool SequentialCommandRunner::IsReady() const {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  return !result_callback_;
}

void SequentialCommandRunner::Cancel() {
  FXL_DCHECK(result_callback_);
  FXL_DCHECK(!status_callback_.IsCanceled());
  FXL_DCHECK(!complete_callback_.IsCanceled());

  Reset();
}

bool SequentialCommandRunner::HasQueuedCommands() const {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  return !command_queue_.empty();
}

void SequentialCommandRunner::RunNextQueuedCommand() {
  FXL_DCHECK(result_callback_);

  if (command_queue_.empty()) {
    NotifyResultAndReset(true);
    return;
  }

  auto next = std::move(command_queue_.front());
  command_queue_.pop();

  status_callback_.Reset([this](CommandChannel::TransactionId, Status status) {
    if (status != Status::kSuccess)
      NotifyResultAndReset(false);
  });

  complete_callback_.Reset([ this, cmd_cb = next.second ](
      CommandChannel::TransactionId, const EventPacket& event_packet) {
    auto status = event_packet.return_params<SimpleReturnParams>()->status;
    if (status != Status::kSuccess) {
      NotifyResultAndReset(false);
      return;
    }

    if (cmd_cb) {
      // We allow the command completion callback (i.e. |cmd_cb|) to cancel its
      // sequence and even immediately start up a new one.
      // SequentialCommandRunner::Cancel() relies on
      // CancelableCallback::Cancel() which would in effect delete this lambda
      // and potentially corrupt its captured environment while executing
      // itself.
      //
      // To prevent that we push the current sequence number and |cmd_cb| itself
      // onto the stack.
      uint64_t prev_seq_no = sequence_number_;
      auto cb = cmd_cb;
      cb(event_packet);

      // The sequence could have been cancelled by |cmd_cb| (and a new sequence
      // could have also started). We make sure here that we are in the correct
      // sequence and terminate if necessary.
      if (!result_callback_ || prev_seq_no != sequence_number_)
        return;
    }

    RunNextQueuedCommand();
  });

  if (!transport_->command_channel()->SendCommand(
          std::move(next.first), task_runner_, complete_callback_.callback(),
          status_callback_.callback())) {
    NotifyResultAndReset(false);
  }
}

void SequentialCommandRunner::Reset() {
  if (!command_queue_.empty())
    command_queue_ = {};
  result_callback_ = nullptr;
  status_callback_.Cancel();
  complete_callback_.Cancel();
}

void SequentialCommandRunner::NotifyResultAndReset(bool result) {
  FXL_DCHECK(result_callback_);
  auto result_cb = result_callback_;
  Reset();
  result_cb(result);
}

}  // namespace hci
}  // namespace bluetooth
