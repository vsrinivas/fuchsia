// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>

#include <lib/async/dispatcher.h>

#include "garnet/drivers/bluetooth/lib/hci/command_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"
#include "lib/fxl/functional/cancelable_callback.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/synchronization/thread_checker.h"

namespace btlib {
namespace hci {

class Transport;

// A SequentialCommandRunner can be used to chain HCI commands one after another
// such that each command in the sequence is sent to the controller only after
// the previous command has completed successfully. When a command fails due to
// an error status (in a HCI_Command_Status or HCI_Command_Complete event) or a
// timeout, the entire sequence is interrupted and an error callback is reported
// back to the caller.
//
// Only commands that terminate with the HCI_Command_Complete event are
// currently supported.
//
// This class is not thread-safe. The dispatcher that is provided during
// initialization must be bound to the thread on which the instance of
// SequentialCommandRunner is being constructed.
class SequentialCommandRunner final {
 public:
  SequentialCommandRunner(async_t* dispatcher,
                          fxl::RefPtr<Transport> transport);
  ~SequentialCommandRunner();

  // Adds a HCI command packet and an optional callback to invoke with the
  // completion event to the command sequence. Cannot be called once
  // RunCommands() has been called.
  using CommandCompleteCallback =
      std::function<void(const EventPacket& command_complete)>;
  void QueueCommand(
      std::unique_ptr<CommandPacket> command_packet,
      const CommandCompleteCallback& callback = CommandCompleteCallback());

  // Runs all the queued commands. Once this is called no new commands can be
  // queued. This method will return before queued commands have been run.
  // |status_callback| is called with the status of the last command run,
  // or kSuccess if all commands returned HCI_Command_Complete.
  //
  // Once RunCommands() has been called this instance will not be ready for
  // re-use until |status_callback| gets run. At that point new commands can be
  // queued and run (see IsReady()).
  //
  // RunCommands() will always send the first queued HCI command to
  // CommandChannel even if it is followed by a call to Cancel().
  using StatusCallback = fit::function<void(Status status)>;
  void RunCommands(StatusCallback status_callback);

  // Returns true if commands can be queued and run on this instance. This
  // returns false if RunCommands() is currently in progress.
  bool IsReady() const;

  // Cancels a running sequence. RunCommands() must have been called before a
  // sequence can be cancelled. Once a sequence is cancelled, the state of the
  // SequentialCommandRunner will be reset (i.e. IsReady() will return true).
  // The result of any pending HCI command will be ignored and callbacks will
  // not be invoked.
  //
  // Depending on the sequence of HCI commands that were previously processed,
  // the controller will be in an undefined state. The caller is responsible for
  // sending successive HCI commands to bring the controller back to an expected
  // state.
  //
  // After a call to Cancel(), this object can be immediately reused to queue up
  // a new HCI command sequence.
  void Cancel();

  // Returns true if this currently has any pending commands.
  bool HasQueuedCommands() const;

 private:
  void RunNextQueuedCommand();
  void Reset();
  void NotifyStatusAndReset(Status status);

  async_t* dispatcher_;
  fxl::RefPtr<Transport> transport_;

  using CommandQueue = std::queue<
      std::pair<std::unique_ptr<CommandPacket>, CommandCompleteCallback>>;
  CommandQueue command_queue_;

  // Callback assigned in RunCommands(). If this is non-null then this object is
  // currently executing a sequence.
  StatusCallback status_callback_;

  // Number assigned to the current sequence. Each "sequence" begins on a call
  // to RunCommands() and ends either on a call to Cancel() or when
  // |status_callback_| has been invoked.
  //
  // This number is used to detect cancelation of a sequence from a
  // CommandCompleteCallback.
  uint64_t sequence_number_;

  fxl::ThreadChecker thread_checker_;

  // The callback we pass to the HCI CommandChannel for command execution.
  // This can be cancelled and erased at any time.
  fxl::CancelableCallback<void(CommandChannel::TransactionId,
                               const EventPacket&)>
      command_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SequentialCommandRunner);
};

}  // namespace hci
}  // namespace btlib
