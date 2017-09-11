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

#include <async/wait.h>
#include <zircon/compiler.h>
#include <zx/channel.h>

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "apps/bluetooth/lib/common/optional.h"
#include "apps/bluetooth/lib/hci/control_packets.h"
#include "apps/bluetooth/lib/hci/hci.h"
#include "apps/bluetooth/lib/hci/hci_constants.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/cancelable_callback.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/synchronization/thread_checker.h"
#include "lib/fxl/tasks/task_runner.h"

namespace bluetooth {
namespace hci {

class Transport;

// Represents the HCI Bluetooth command channel. Manages HCI command and event
// packet control flow.
//
// TODO(armansito): I don't imagine many cases in which we will want to queue up HCI commands from
// the data thread. Consider making this class fully single threaded and removing the locks.
class CommandChannel final {
 public:
  // |hci_command_channel| is a Zircon channel construct that can receive
  // Bluetooth HCI command and event packets, in which the remote end is
  // implemented by the underlying Bluetooth HCI device driver.
  //
  // |transport| is the Transport instance that owns this CommandChannel.
  CommandChannel(Transport* transport, zx::channel hci_command_channel);
  ~CommandChannel();

  // Starts listening on the HCI command channel and starts handling commands and events.
  void Initialize();

  // Unregisters event handlers and cleans up.
  // NOTE: Initialize() and ShutDown() MUST be called on the same thread. These methods are not
  // thread-safe.
  void ShutDown();

  // Used to identify an individual HCI command<->event transaction.
  using TransactionId = size_t;

  // Callback invoked to report the completion of a HCI command.
  using CommandCompleteCallback =
      std::function<void(TransactionId id, const EventPacket& event_packet)>;

  // Callback invoked to report the status of a pending HCI command. This can be following the
  // receipt of a HCI_Command_Status event from the controller OR due to a command timeout.
  using CommandStatusCallback = std::function<void(TransactionId id, Status status)>;

  // Queues the given |command_packet| to be sent to the controller and returns
  // a transaction ID. The given callbacks will be posted on |task_runner| to be
  // processed on the appropriate thread requested by the caller.
  //
  // This call will take ownership of the contents of |command_packet|. |command_packet| MUST
  // represent a valid HCI command packet.
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
  // When a caller provides a value for |complete_event_code| the caller can also
  // optionally provide a filter |complete_event_matcher| which will be used to determine whether
  // the event should match this command sequence. If a filter rejects the event (by returning
  // false), the event will be passed to the handler registered via AddEventHander(). This callback
  // will be run on the I/O thread and must complete synchronously.
  //
  // Returns a transaction ID that is unique to the initiated command sequence.
  // This can be used to identify the command sequence by comparing it to the
  // |id| parameter in a CommandCompleteCallback.
  //
  // If the controller does not respond to with the expected |complete_event_code| within a certain
  // amount of time (see kCommandTimeoutMs in hci_constants.h) the command will time out. This will
  // be signalled to the caller by invoking |status_callback| with the |status| parameter set to
  // the special status code Status::kCommandTimeout.
  //
  // See Bluetooth Core Spec v5.0, Volume 2, Part E, Section 4.4 "Command Flow
  // Control" for more information about the HCI command flow control.
  using EventMatcher = std::function<bool(const EventPacket& event)>;
  TransactionId SendCommand(std::unique_ptr<CommandPacket> command_packet,
                            fxl::RefPtr<fxl::TaskRunner> task_runner,
                            const CommandCompleteCallback& complete_callback,
                            const CommandStatusCallback& status_callback = {},
                            const EventCode complete_event_code = kCommandCompleteEventCode,
                            const EventMatcher& complete_event_matcher = {});

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
  // If |task_runner| corresponds to the I/O thread's task runner, then the callback will be
  // executed as soon as the event is received from the command channel. No delayed task posting
  // will occur.
  //
  // The following values for |event_code| cannot be passed to this method:
  //    - HCI_Command_Complete event code
  //    - HCI_Command_Status event code
  //    - HCI_LE_Meta event code (use AddLEMetaEventHandler instead).
  EventHandlerId AddEventHandler(EventCode event_code, const EventCallback& event_callback,
                                 fxl::RefPtr<fxl::TaskRunner> task_runner);

  // Works just like AddEventHandler but the passed in event code is only valid within the LE Meta
  // Event sub-event code namespace. |event_callback| will get invoked whenever the controller sends
  // a LE Meta Event with a matching subevent code.
  //
  // |subevent_code| cannot be 0.
  EventHandlerId AddLEMetaEventHandler(EventCode subevent_code, const EventCallback& event_callback,
                                       fxl::RefPtr<fxl::TaskRunner> task_runner);

  // Removes a previously registered event handler. Does nothing if an event
  // handler with the given |id| could not be found.
  void RemoveEventHandler(EventHandlerId id);

  // Returns the underlying channel handle.
  const zx::channel& channel() const { return channel_; }

 private:
  // Represents a pending HCI command.
  struct PendingTransactionData {
    TransactionId id;
    OpCode opcode;
    EventCode complete_event_code;
    EventMatcher complete_event_matcher;
    CommandStatusCallback status_callback;
    CommandCompleteCallback complete_callback;
    fxl::RefPtr<fxl::TaskRunner> task_runner;
  };

  // Represents a queued command packet.
  struct QueuedCommand {
    QueuedCommand(TransactionId id, std::unique_ptr<CommandPacket> command_packet,
                  const CommandStatusCallback& status_callback,
                  const CommandCompleteCallback& complete_callback,
                  fxl::RefPtr<fxl::TaskRunner> task_runner, EventCode complete_event_code,
                  const EventMatcher& complete_event_matcher);
    QueuedCommand() = default;

    QueuedCommand(QueuedCommand&& other) = default;
    QueuedCommand& operator=(QueuedCommand&& other) = default;

    std::unique_ptr<CommandPacket> packet;
    PendingTransactionData transaction_data;
  };

  // Data stored for each event handler registered via AddEventHandler.
  struct EventHandlerData {
    EventHandlerId id;
    EventCode event_code;
    EventCallback event_callback;
    bool is_le_meta_subevent;
    fxl::RefPtr<fxl::TaskRunner> task_runner;
  };

  // Tries to send the next queued command if there are any queued commands and
  // there is no currently pending command.
  void TrySendNextQueuedCommand();

  // If the given event packet corresponds to the currently pending command,
  // this method completes the transaction and sends the next queued command, if
  // any. Returns false if the event needs to be handled by an event handler and does not take
  // ownership of it.
  bool HandlePendingCommandComplete(std::unique_ptr<EventPacket>&& event);

  // If the given CommandStatus event packet corresponds to the currently
  // pending command and notifes the transaction's |status_callback|.
  void HandlePendingCommandStatus(const EventPacket& event);

  // Returns a pointer to the currently pending command. Return nullptr if no
  // command is currently pending.
  PendingTransactionData* GetPendingCommand();

  // Sets the currently pending command. If |command| is nullptr, this will
  // clear the currently pending command. This also cancels the HCI command timeout callback for the
  // current pending command.
  void SetPendingCommand(PendingTransactionData* command);

  // Creates a new event handler entry in the event handler map and returns its ID.
  EventHandlerId NewEventHandler(EventCode event_code, bool is_le_meta,
                                 const EventCallback& event_callback,
                                 fxl::RefPtr<fxl::TaskRunner> task_runner)
      __TA_REQUIRES(event_handler_mutex_);

  // Notifies a matching event handler for the given event.
  void NotifyEventHandler(std::unique_ptr<EventPacket> event);

  // Read ready handler for |channel_|
  async_wait_result_t OnChannelReady(async_t* async, zx_status_t status,
                                     const zx_packet_signal_t* signal);

  // TransactionId counter.
  std::atomic_size_t next_transaction_id_ __TA_GUARDED(send_queue_mutex_);

  // EventHandlerId counter.
  std::atomic_size_t next_event_handler_id_ __TA_GUARDED(event_handler_mutex_);

  // Used to assert that certain public functions are only called on the creation thread.
  fxl::ThreadChecker thread_checker_;

  // The Transport object that owns this CommandChannel.
  Transport* transport_;  // weak

  // The channel we use to send/receive HCI commands/events.
  zx::channel channel_;

  // Wait object for |channel_|
  async::Wait channel_wait_;

  // True if this CommandChannel has been initialized through a call to Initialize().
  std::atomic_bool is_initialized_;

  // The task runner used for posting tasks on the HCI transport I/O thread.
  fxl::RefPtr<fxl::TaskRunner> io_task_runner_;

  // Guards |send_queue_|. |send_queue_| can get accessed by threads that call
  // SendCommand() as well as from |io_thread_|.
  std::mutex send_queue_mutex_;

  // The HCI command queue. These are the commands that have been queued to be
  // sent to the controller.
  // TODO(armansito): Store std::unique_ptr<QueuedCommand>?
  std::queue<QueuedCommand> send_queue_ __TA_GUARDED(send_queue_mutex_);

  // Contains the currently pending HCI command packet. While controllers may
  // allow more than one packet to be pending at a given point in time, we only
  // send one packet at a time to keep things simple.
  //
  // Accessed only from the I/O thread and thus not guarded.
  common::Optional<PendingTransactionData> pending_command_;

  // The command timeout callback assigned to the current pending command.
  fxl::CancelableClosure pending_cmd_timeout_;

  // Guards |event_handler_id_map_| and |event_code_handlers_| which can be
  // accessed by both the public EventHandler methods and |io_thread_|.
  std::mutex event_handler_mutex_;

  // Mapping from event handler IDs to handler data.
  std::unordered_map<EventHandlerId, EventHandlerData> event_handler_id_map_
      __TA_GUARDED(event_handler_mutex_);

  // Mapping from event code to the event handler that was registered to handle
  // that event code.
  std::unordered_map<EventCode, EventHandlerId> event_code_handlers_
      __TA_GUARDED(event_handler_mutex_);

  // Mapping from LE Meta Event Subevent code to the event handler that was registered to handle
  // that event code.
  std::unordered_map<EventCode, EventHandlerId> subevent_code_handlers_
      __TA_GUARDED(event_handler_mutex_);

  FXL_DISALLOW_COPY_AND_ASSIGN(CommandChannel);
};

}  // namespace hci
}  // namespace bluetooth
