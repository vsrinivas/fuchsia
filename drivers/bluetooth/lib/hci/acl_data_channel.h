// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <mutex>
#include <queue>

#include <magenta/compiler.h>
#include <mx/channel.h>

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "apps/bluetooth/lib/hci/acl_data_packet.h"
#include "apps/bluetooth/lib/hci/command_channel.h"
#include "apps/bluetooth/lib/hci/control_packets.h"
#include "apps/bluetooth/lib/hci/hci_constants.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/synchronization/thread_checker.h"
#include "lib/ftl/tasks/task_runner.h"

namespace bluetooth {
namespace hci {

class Connection;
class Transport;

// Represents the controller data buffer settings for the BR/EDR or LE transports.
class DataBufferInfo {
 public:
  // Initialize fields to non-zero values.
  DataBufferInfo(size_t max_data_length, size_t max_num_packets);

  // The default constructor sets all fields to zero. This can be used to represent a data buffer
  // that does not exist (e.g. the controller has a single shared buffer and no dedicated LE buffer.
  DataBufferInfo();

  // The maximum length (in octets) of the data portion of each HCI ACL data packet that the
  // controller is able to accept.
  size_t max_data_length() const { return max_data_length_; }

  // Returns the total number of HCI ACL data packets that can be stored in the data buffer
  // reprented by this object.
  size_t max_num_packets() const { return max_num_packets_; }

  // Returns true if both fields are set to zero.
  bool IsAvailable() const { return max_data_length_ && max_num_packets_; }

  // Comparison operators.
  bool operator==(const DataBufferInfo& other) const;
  bool operator!=(const DataBufferInfo& other) const { return !(*this == other); }

 private:
  size_t max_data_length_;
  size_t max_num_packets_;
};

// Represents the Bluetooth ACL Data channel and manages the Host<->Controller ACL data flow
// control.
//
// This currently only supports the Packet-based Data Flow Control as defined in Core Spec v5.0, Vol
// 2, Part E, Section 4.1.1.
class ACLDataChannel final : public ::mtl::MessageLoopHandler {
 public:
  // The ACLDataChannel may need to look up connections using their handles for proper flow control.
  // NOTE: Implementations should be thread-safe as this will be invoked from the I/O thread.
  // NOTE: Implementations should avoid calling the public interface methods of the ACLDataChannel
  //       to avoid causing a potential deadlock.
  using ConnectionLookupCallback = std::function<ftl::RefPtr<Connection>(ConnectionHandle)>;

  ACLDataChannel(Transport* transport, mx::channel hci_acl_channel,
                 const ConnectionLookupCallback& conn_lookup_cb);
  ~ACLDataChannel() override;

  // Starts listening on the HCI ACL data channel and starts handling data flow control.
  // |bredr_buffer_info| represents the controller's data buffering capacity for the
  // BR/EDR transport and the |le_buffer_info| represents Low Energy buffers. At least one of these
  // (BR/EDR vs LE) must contain non-zero values. Generally rules of thumb:
  //
  //   - A LE only controller will have LE buffers only.
  //   - A BR/EDR-only controller will have BR/EDR buffers only.
  //   - A dual-mode controller will have BR/EDR buffers and MAY have LE buffers if the BR/EDR
  //     buffer is not shared between the transports.
  //
  // As this class is intended to support flow-control for both, this function should be called
  // based on what is reported by the controller.
  void Initialize(const DataBufferInfo& bredr_buffer_info, const DataBufferInfo& le_buffer_info);

  // Unregisters event handlers and cleans up.
  // NOTE: Initialize() and ShutDown() MUST be called on the same thread. These methods are not
  // thread-safe.
  void ShutDown();

  // Callback invoked when there is a new ACL data packet from the controller. The ownership of the
  // |data_packet| is passed to the callback implementation as a rvalue reference..
  using DataReceivedCallback = std::function<void(std::unique_ptr<ACLDataPacket> data_packet)>;

  // Assigns a handler callback for received ACL data packets.
  void SetDataRxHandler(const DataReceivedCallback& rx_callback,
                        ftl::RefPtr<ftl::TaskRunner> rx_task_runner);

  // Queues the given ACL data packet to be sent to the controller. Returns false if the packet
  // cannot be queued up, e.g. if |data_packet| does not correspond to a known link layer
  // connection.
  //
  // |data_packet| is passed by value, meaning that ACLDataChannel will take ownership of it.
  // |data_packet| must represent a valid ACL data packet.
  bool SendPacket(std::unique_ptr<ACLDataPacket> data_packet);

  // Returns the underlying channel handle.
  const mx::channel& channel() const { return channel_; }

  // Returns the BR/EDR buffer information that the channel was initialized with.
  const DataBufferInfo& GetBufferInfo() const;

  // Returns the LE buffer information that the channel was initialized with. This defaults to the
  // BR/EDR buffers if the controller does not have a dedicated LE buffer.
  const DataBufferInfo& GetLEBufferInfo() const;

 private:
  // Represents a queued ACL data packet.
  struct QueuedDataPacket {
    QueuedDataPacket() = default;
    QueuedDataPacket(QueuedDataPacket&& other) = default;
    QueuedDataPacket& operator=(QueuedDataPacket&& other) = default;

    std::unique_ptr<ACLDataPacket> packet;
  };

  // Returns the data buffer MTU for the given connection.
  size_t GetBufferMTU(const Connection& connection);

  // Handler for the HCI Number of Completed Packets Event, used for packet-based data flow control.
  void NumberOfCompletedPacketsCallback(const EventPacket& event);

  // Tries to send the next batch of queued data packets if the controller has any space available.
  void TrySendNextQueuedPackets();

  // Returns the number of BR/EDR packets for which the controller has available space to buffer.
  size_t GetNumFreeBREDRPacketsLocked() const __TA_REQUIRES(send_mutex_);

  // Returns the number of LE packets for which controller has available space to buffer. Must be
  // called from a locked context.
  size_t GetNumFreeLEPacketsLocked() const __TA_REQUIRES(send_mutex_);

  // Decreases the total number of sent packets count by the given amount. Must be called from a
  // locked context.
  void DecrementTotalNumPacketsLocked(size_t count) __TA_REQUIRES(send_mutex_);

  // Decreases the total number of sent packets count for LE by the given amount. Must be called
  // from a locked context.
  void DecrementLETotalNumPacketsLocked(size_t count) __TA_REQUIRES(send_mutex_);

  // Increments the total number of sent packets count by the given amount. Must be called from a
  // locked context.
  void IncrementTotalNumPacketsLocked(size_t count) __TA_REQUIRES(send_mutex_);

  // Increments the total number of sent LE packets count by the given amount. Must be called from a
  // locked context.
  void IncrementLETotalNumPacketsLocked(size_t count) __TA_REQUIRES(send_mutex_);

  // Returns true if the maximum number of sent packets has been reached. Must be called from a
  // locked context.
  bool MaxNumPacketsReachedLocked() const __TA_REQUIRES(send_mutex_);

  // Returns true if the maximum number of sent LE packets has been reached. Must be called from a
  // locked context.
  bool MaxLENumPacketsReachedLocked() const __TA_REQUIRES(send_mutex_);

  // ::mtl::MessageLoopHandler overrides:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending, uint64_t count) override;
  void OnHandleError(mx_handle_t handle, mx_status_t error) override;

  // Used to assert that certain public functions are only called on the creation thread.
  ftl::ThreadChecker thread_checker_;

  // The Transport object that owns this instance.
  Transport* transport_;  // weak;

  // The channel that we use to send/receive HCI ACL data packets.
  mx::channel channel_;

  // The callback used to obtain references to Connection objects based on a link-layer connection
  // handle.
  ConnectionLookupCallback conn_lookup_cb_;

  // True if this instance has been initialized through a call to Initialize().
  std::atomic_bool is_initialized_;

  // The event handler ID for the Number Of Completed Packets event.
  CommandChannel::EventHandlerId event_handler_id_;

  // The HandlerKey returned from mtl::MessageLoop::AddHandler
  mtl::MessageLoop::HandlerKey io_handler_key_;

  // The task runner used for posting tasks on the HCI transport I/O thread.
  ftl::RefPtr<ftl::TaskRunner> io_task_runner_;

  // The current handler for incoming data and the task runner on which to run it.
  std::mutex rx_mutex_;
  DataReceivedCallback rx_callback_ __TA_GUARDED(rx_mutex_);
  ftl::RefPtr<ftl::TaskRunner> rx_task_runner_ __TA_GUARDED(rx_mutex_);

  // BR/EDR data buffer information. This buffer will not be available on LE-only controllers.
  DataBufferInfo bredr_buffer_info_;

  // LE data buffer information. This buffer will not be available on BR/EDR-only controllers (which
  // we do not support) and MAY be available on dual-mode controllers. We maintain that if this
  // buffer is not available, then the BR/EDR buffer MUST be available.
  DataBufferInfo le_buffer_info_;

  // Mutex that guards access to data transmission related members below.
  std::mutex send_mutex_;

  // The current count of the number of ACL data packets that have been sent to the controller.
  // |le_num_sent_packets_| is ignored if the controller uses one buffer for LE and BR/EDR.
  size_t num_sent_packets_ __TA_GUARDED(send_mutex_);
  size_t le_num_sent_packets_ __TA_GUARDED(send_mutex_);

  // The ACL data packet queue contains the data packets that are waiting to be sent to the
  // controller.
  // TODO(armansito): We'll probably need a smarter priority queue here since a simple FIFO queue
  // can cause a single connection to starve others when there are multiple connections present. We
  // should instead manage data packet priority based on its connection handle and on the result of
  // the HCI Number of Completed Packets events.
  std::queue<QueuedDataPacket> send_queue_ __TA_GUARDED(send_mutex_);

  FTL_DISALLOW_COPY_AND_ASSIGN(ACLDataChannel);
};

}  // namespace hci
}  // namespace bluetooth
