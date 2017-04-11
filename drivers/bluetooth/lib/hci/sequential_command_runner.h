// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace bluetooth {
namespace hci {

class EventPacket;
class Transport;

// A SequentialCommandRunner can be used to chain HCI commands one after another such that each
// command in the sequence is sent to the controller only after the previous command has completed
// successfully. When a command fails due to an error status (in a HCI_Command_Status or
// HCI_Command_Complete event) or a timeout, the entire sequence is interrupted and an error
// callback is reported back to the caller.
//
// Only commands that terminate with the HCI_Command_Complete event are currently supported.
//
// This class is not thread-safe. The TaskRunner that is provided during initialization must be
// bound to the thread on which the instance of SequentialCommandRunner is being constructed.
class SequentialCommandRunner final {
 public:
  SequentialCommandRunner(ftl::RefPtr<ftl::TaskRunner> task_runner,
                          ftl::RefPtr<Transport> transport);
  ~SequentialCommandRunner();

  // Adds a HCI command packet and an optional callback to invoke with the completion event to the
  // command sequence. Cannot be called once RunCommands() has been called.
  using CommandCompleteCallback = std::function<void(const EventPacket& command_complete)>;
  void QueueCommand(common::DynamicByteBuffer command_packet,
                    const CommandCompleteCallback& callback = CommandCompleteCallback());

  // Runs all the queued commands. Once this is called no new commands can be queued. This method
  // will return before all queued commands have been run which is signaled by invoking
  // |result_callback| asynchronously.
  //
  // Once RunCommands() has been called this instance will not be ready for re-use until
  // |result_callback| gets run. At that point new commands can be queued and run (see IsReady()).
  using ResultCallback = std::function<void(bool success)>;
  void RunCommands(const ResultCallback& result_callback);

  // Returns true if commands can be queued and run on this instance. This returns false if
  // RunCommands() is currently in progress.
  bool IsReady() const;

  // Returns true if this currently has any pending commands.
  bool HasQueuedCommands() const;

 private:
  void RunNextQueuedCommand();
  void NotifyResultAndReset(bool result);

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  ftl::RefPtr<Transport> transport_;
  ResultCallback result_callback_;

  using CommandQueue = std::queue<std::pair<common::DynamicByteBuffer, CommandCompleteCallback>>;
  CommandQueue command_queue_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  ftl::WeakPtrFactory<SequentialCommandRunner> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SequentialCommandRunner);
};

}  // namespace hci
}  // namespace bluetooth
