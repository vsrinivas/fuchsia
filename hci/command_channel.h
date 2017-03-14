// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
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
class Transport;

// Represents the HCI Bluetooth command channel. Manages HCI command and event
// packet control flow.
class CommandChannel final : public ::mtl::MessageLoopHandler {
 public:
  // |hci_command_channel| is a Magenta channel construct that can receive
  // Bluetooth HCI command and event packets, in which the remote end is
  // implemented by the underlying Bluetooth HCI device driver.
  //
  // |transport| is the Transport instance that owns this CommandChannel.
  CommandChannel(Transport* transport, mx::channel hci_command_channel);
  virtual ~CommandChannel();

  // Starts listening on the HCI command channel and starts handling commands and events.
  void Initialize();

  // Unregisters event handlers and cleans up.
  void ShutDown();

  // Used to identify an individual HCI command<->event transaction.
  using TransactionId = size_t;

  // Callback invoked to report the completion of a HCI command.
  using CommandCompleteCallback =
      std::function<void(TransactionId id, const EventPacket& event_packet)>;

  // Callback invoked to report the status of a pending HCI command.
  using CommandStatusCallback = std::function<void(TransactionId id, Status status)>;

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
  TransactionId SendCommand(const CommandPacket& command_packet,
                            const CommandStatusCallback& status_callback,
                            const CommandCompleteCallback& complete_callback,
                            ftl::RefPtr<ftl::TaskRunner> task_runner,
                            const EventCode complete_event_code = kCommandCompleteEventCode);

  // Used to identify an individual HCI event handler that was registered with
  // this CommandChannel.
  using EventHandlerId = size_t;

  // Callback invoked to report generic HCI events excluding CommandComplete and
  // CommandStatus events.
  using EventCallback = std::function<void(const EventPacket& event_packet)>;

  // Registers an event handler for HCI events that match |event_code|. Incoming
  // HCI event packets that are not associated with a pending command sequence
  // will be posted on the given |task_runner| via the given |event_callback|.
  // The returned ID can be used to unregister a previously registered event
  // handler.
  //
  // |event_callback| will be invoked for all HCI event packets that match
  // |event_code|, except for:
  //   - HCI_CommandStatus events;
  //   - HCI_CommandComplete events;
  //   - The completion event of the currently pending command packet, if any;
  //
  // Returns a non-zero ID if the handler was successfully registered. Returns
  // zero in case of an error.
  //
  // Only one handler can be registered for a given |event_code| at a time. If a
  // handler was previously registered for the given |event_code|, this method
  // returns zero.
  //
  // The following values for |event_code| cannot be passed to this method:
  //    - HCI_Command_Complete event code
  //    - HCI_Command_Status event code
  //    - HCI_LE_Meta event code (use AddLEMetaEventHandler instead).
  EventHandlerId AddEventHandler(EventCode event_code, const EventCallback& event_callback,
                                 ftl::RefPtr<ftl::TaskRunner> task_runner);

  // Works just like AddEventHandler but the passed in event code is only valid within the LE Meta
  // Event sub-event code namespace. |event_callback| will get invoked whenever the controller sends
  // a LE Meta Event with a matching subevent code.
  //
  // |subevent_code| cannot be 0.
  EventHandlerId AddLEMetaEventHandler(EventCode subevent_code, const EventCallback& event_callback,
                                       ftl::RefPtr<ftl::TaskRunner> task_runner);

  // Removes a previously registered event handler. Does nothing if an event
  // handler with the given |id| could not be found.
  void RemoveEventHandler(EventHandlerId id);

 private:
  // TransactionId counter.
  static std::atomic_size_t next_transaction_id_;

  // EventHandlerId counter.
  static std::atomic_size_t next_event_handler_id_;

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
    QueuedCommand(TransactionId id, const CommandPacket& command_packet,
                  const CommandStatusCallback& status_callback,
                  const CommandCompleteCallback& complete_callback,
                  ftl::RefPtr<ftl::TaskRunner> task_runner, EventCode complete_event_code);
    QueuedCommand() = default;

    QueuedCommand(QueuedCommand&& other) = default;
    QueuedCommand& operator=(QueuedCommand&& other) = default;

    common::DynamicByteBuffer packet_data;
    PendingTransactionData transaction_data;
  };

  // Data stored for each event handler registered via AddEventHandler.
  struct EventHandlerData {
    EventHandlerId id;
    EventCode event_code;
    EventCallback event_callback;
    bool is_le_meta_subevent;
    ftl::RefPtr<ftl::TaskRunner> task_runner;
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

  // Notifies a matching event handler for the given event.
  void NotifyEventHandler(const EventPacket& event);

  // ::mtl::MessageLoopHandler overrides:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override;
  void OnHandleError(mx_handle_t handle, mx_status_t error) override;

  // The Transport object that owns this CommandChannel.
  Transport* transport_;  // weak

  // The channel we use to send/receive HCI commands/events.
  mx::channel channel_;

  // True if this CommandChannel has been initialized through a call to Initialize().
  std::atomic_bool is_initialized_;

  // The HandlerKey returned from mtl::MessageLoop::AddHandler
  mtl::MessageLoop::HandlerKey io_handler_key_;

  // The task runner used for posting tasks on the HCI transport I/O thread.
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
  common::StaticByteBuffer<EventPacket::GetMinBufferSize(kMaxEventPacketPayloadSize)> event_buffer_;

  // Mapping from event handler IDs to handler data.
  std::unordered_map<EventHandlerId, EventHandlerData> event_handler_id_map_;

  // Mapping from event code to the event handler that was registered to handle
  // that event code.
  std::unordered_map<EventCode, EventHandlerId> event_code_handlers_;

  // Mapping from LE Meta Event Subevent code to the event handler that was registered to handle
  // that event code.
  std::unordered_map<EventCode, EventHandlerId> subevent_code_handlers_;

  // Guards |event_handler_id_map_| and |event_code_handlers_| which can be
  // accessed by both the public EventHandler methods and |io_thread_|.
  std::mutex event_handler_mutex_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CommandChannel);
};

}  // namespace hci
}  // namespace bluetooth
