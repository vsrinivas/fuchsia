// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include <mx/channel.h>

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

#include "apps/bluetooth/common/byte_buffer.h"
#include "apps/bluetooth/hci/event_packet.h"
#include "apps/bluetooth/hci/hci.h"
#include "apps/bluetooth/hci/hci_constants.h"

namespace bluetooth {
namespace hci {

class CommandPacket;

// Represents the HCI Bluetooth command channel. Manages HCI command and event
// packet control flow.
class CommandChannel final : public ::mtl::MessageLoopHandler {
 public:
  // Used to identify an individual HCI command<->event transaction.
  using TransactionId = size_t;

  // |hci_command_channel| is a Magenta channel construct that can receive
  // Bluetooth HCI command and event packets, in which the remote end is
  // implemented by the underlying Bluetooth HCI device driver.
  explicit CommandChannel(mx::channel hci_command_channel);
  virtual ~CommandChannel();

  // Starts the I/O event loop. This kicks off a new I/O thread for
  // this channel instance. Care must be taken such that the public methods of
  // this class are not called in a manner that would race with the execution of
  // Initialize().
  void Initialize();

  // This stops the I/O event loop and joins the I/O thread.
  // NOTE: Care must be taken such that this method is not called from a thread
  // that would race with a call to Initialize(). ShutDown() is not thread-safe
  // and should not be called from multiple threads at the same time.
  void ShutDown();

  // Callback invoked to report the completion of a HCI command.
  using CommandCompleteCallback =
      std::function<void(TransactionId id, const EventPacket& event_packet)>;

  // Callback invoked to report the status of a pending HCI command.
  using CommandStatusCallback =
      std::function<void(TransactionId id, Status status)>;

  // Queues the given |command_packet| to be sent to the controller and returns
  // a transaction ID. The given callbacks will be posted on |task_runner| to be
  // processed on the appropriate thread requested by the caller.
  //
  // The contents of the given |command_packet| and the underlying buffer will
  // be undefined after this function exits successfully, as the underlying
  // buffer may be moved for efficient queuing of packet contents.
  //
  // |status_callback| will be called if the controller responds to the command
  // with a CommandStatus HCI event.
  //
  // |complete_callback| will be called if the controller responds to the
  // command with an event with the given |complete_event_code|. Most HCI
  // commands are marked as complete using the CommandComplete HCI event,
  // however some command sequences use different events, as specified in the
  // Bluetooth Core Specification.
  //
  // Returns a transaction ID that is unique to the initiated command sequence.
  // This can be used to identify the command sequence by comparing it to the
  // |id| parameter in a CommandCompleteCallback.
  //
  // See Bluetooth Core Spec v5.0, Volume 2, Part E, Section 4.4 "Command Flow
  // Control" for more information about the HCI command flow control.
  TransactionId SendCommand(
      const CommandPacket& command_packet,
      const CommandStatusCallback& status_callback,
      const CommandCompleteCallback& complete_callback,
      ftl::RefPtr<ftl::TaskRunner> task_runner,
      const EventCode complete_event_code = kCommandCompleteEventCode);

 private:
  // TransactionId counter.
  static std::atomic_size_t next_transaction_id_;

  // Represents a pending HCI command.
  struct PendingTransactionData {
    TransactionId id;
    OpCode opcode;
    EventCode complete_event_code;
    CommandStatusCallback status_callback;
    CommandCompleteCallback complete_callback;
    ftl::RefPtr<ftl::TaskRunner> task_runner;
  };

  // Represents a queued command packet.
  struct QueuedCommand {
    QueuedCommand(TransactionId id,
                  const CommandPacket& command_packet,
                  const CommandStatusCallback& status_callback,
                  const CommandCompleteCallback& complete_callback,
                  ftl::RefPtr<ftl::TaskRunner> task_runner,
                  const EventCode complete_event_code);
    QueuedCommand() = default;

    QueuedCommand(QueuedCommand&& other) = default;
    QueuedCommand& operator=(QueuedCommand&& other) = default;

    common::DynamicByteBuffer packet_data;
    PendingTransactionData transaction_data;
  };

  // Tries to send the next queued command if there are any queued commands and
  // there is no currently pending command.
  void TrySendNextQueuedCommand();

  // If the given event packet corresponds to the currently pending command,
  // this method completes the transaction and sends the next queued command, if
  // any.
  void HandlePendingCommandComplete(const EventPacket& event);

  // If the given CommandStatus event packet corresponds to the currently
  // pending command and notifes the transaction's |status_callback|.
  void HandlePendingCommandStatus(const EventPacket& event);

  // Returns a pointer to the currently pending command. Return nullptr if no
  // command is currently pending.
  PendingTransactionData* GetPendingCommand();

  // Sets the currently pending command. If |command| is nullptr, this will
  // clear the currently pending command.
  void SetPendingCommand(PendingTransactionData* command);

  // ::mtl::MessageLoopHandler overrides:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override;
  void OnHandleError(mx_handle_t handle, mx_status_t error) override;

  // The channel we use to send/receive HCI commands/events.
  mx::channel channel_;

  // True if the I/O event loop is currently running.
  std::atomic_bool is_running_;

  // The thread on which the command channel event loop runs.
  std::thread io_thread_;

  // The HandlerKey returned from mtl::MessageLoop::AddHandler
  mtl::MessageLoop::HandlerKey io_handler_key_;

  // The task runner used for posting tasks on |io_thread_|.
  ftl::RefPtr<ftl::TaskRunner> io_task_runner_;

  // The HCI command queue. These are the commands that have been queued to be
  // sent to the controller.
  std::queue<QueuedCommand> send_queue_;

  // Guards |send_queue_|. |send_queue_| can get accessed by threads that call
  // SendCommand() as well as from |io_thread_|.
  std::mutex send_queue_mutex_;

  // Contains the currently pending HCI command packet. While controllers may
  // allow more than one packet to be pending at a given point in time, we only
  // send one packet at a time to keep things simple.
  //
  // Accessed only from the I/O thread and thus not guarded.
  PendingTransactionData pending_command_;

  // Field indicating whether or not there is currently a pending command.
  bool is_command_pending_;

  // Buffer where we queue incoming HCI event packets.
  common::StaticByteBuffer<EventPacket::GetMinBufferSize(
      kMaxEventPacketPayloadSize)>
      event_buffer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CommandChannel);
};

}  // namespace hci
}  // namespace bluetooth
