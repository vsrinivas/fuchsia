// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sequential_command_runner.h"

#include "command_channel.h"
#include "hci.h"
#include "transport.h"

#include "lib/fxl/functional/make_copyable.h"

namespace btlib {
namespace hci {

using common::HostError;

SequentialCommandRunner::SequentialCommandRunner(
    async_t* dispatcher, fxl::RefPtr<Transport> transport)
    : dispatcher_(dispatcher),
      transport_(transport),
      sequence_number_(0u),
      running_commands_(0u),
      weak_ptr_factory_(this) {
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(transport_);
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
}

SequentialCommandRunner::~SequentialCommandRunner() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
}

void SequentialCommandRunner::QueueCommand(
    std::unique_ptr<CommandPacket> command_packet,
    CommandCompleteCallback callback, bool wait) {
  FXL_DCHECK(!status_callback_);
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(sizeof(CommandHeader) <= command_packet->view().size());

  command_queue_.emplace(
      QueuedCommand{std::move(command_packet), std::move(callback), wait});
}

void SequentialCommandRunner::RunCommands(StatusCallback status_callback) {
  FXL_DCHECK(!status_callback_);
  FXL_DCHECK(status_callback);
  FXL_DCHECK(!command_queue_.empty());
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  status_callback_ = std::move(status_callback);
  sequence_number_++;

  TryRunNextQueuedCommand();
}

bool SequentialCommandRunner::IsReady() const {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  return !status_callback_;
}

void SequentialCommandRunner::Cancel() {
  FXL_DCHECK(status_callback_);
  status_callback_(Status(common::HostError::kCanceled));
  Reset();
}

bool SequentialCommandRunner::HasQueuedCommands() const {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  return !command_queue_.empty();
}

void SequentialCommandRunner::TryRunNextQueuedCommand(Status status) {
  FXL_DCHECK(status_callback_);

  //. If an error occurred or we're done, reset.
  if (!status || (command_queue_.empty() && running_commands_ == 0)) {
    NotifyStatusAndReset(std::move(status));
    return;
  }

  // Wait for the rest of the running commands to finish if we need to.
  if (command_queue_.empty() ||
      (running_commands_ > 0 && command_queue_.front().wait)) {
    return;
  }

  QueuedCommand next = std::move(command_queue_.front());
  command_queue_.pop();

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto command_callback = [self, cmd_cb = std::move(next.callback),
                           seq_no = sequence_number_](
                              auto, const EventPacket& event_packet) {
    auto status = event_packet.ToStatus();
    if (status && event_packet.event_code() == kCommandStatusEventCode) {
      return;
    }

    // TODO(NET-682): Allow async commands to be queued.
    FXL_DCHECK(!status ||
               event_packet.event_code() == kCommandCompleteEventCode);

    if (cmd_cb) {
      cmd_cb(event_packet);
    }

    // The sequence could have been cancelled (and a new sequence could have
    // also started). Make sure here that we are in the correct sequence.
    if (!self || !self->status_callback_ || seq_no != self->sequence_number_) {
      return;
    }
    FXL_DCHECK(self->running_commands_ > 0);
    self->running_commands_--;
    self->TryRunNextQueuedCommand(status);
  };

  running_commands_++;
  if (!transport_->command_channel()->SendCommand(
          std::move(next.packet), dispatcher_, std::move(command_callback))) {
    NotifyStatusAndReset(Status(HostError::kFailed));
  } else {
    TryRunNextQueuedCommand();
  }
}

void SequentialCommandRunner::Reset() {
  if (!command_queue_.empty()) {
    command_queue_ = {};
  }
  running_commands_ = 0;
  status_callback_ = nullptr;
}

void SequentialCommandRunner::NotifyStatusAndReset(Status status) {
  FXL_DCHECK(status_callback_);
  auto status_cb = std::move(status_callback_);
  Reset();
  status_cb(status);
}

}  // namespace hci
}  // namespace btlib
