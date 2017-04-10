// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <mutex>
#include <queue>

#include <mx/channel.h>

#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "apps/bluetooth/lib/hci/acl_data_packet.h"
#include "apps/bluetooth/lib/hci/command_channel.h"
#include "apps/bluetooth/lib/hci/hci_constants.h"

namespace bluetooth {
namespace hci {

class Connection;
class EventPacket;
class Transport;

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

  // Callback invoked when there is a new ACL data packet from the controller. The ownership of the
  // |acl_data_packet| is passed to the callback implementation.
  using DataReceivedCallback = std::function<void(common::DynamicByteBuffer acl_data_packet)>;

  ACLDataChannel(Transport* transport, mx::channel hci_acl_channel,
                 const ConnectionLookupCallback& conn_lookup_cb,
                 const DataReceivedCallback& rx_callback,
                 ftl::RefPtr<ftl::TaskRunner> rx_task_runner);
  ~ACLDataChannel() override;

  // Starts listening on the HCI ACL data channel and starts handling data flow control.
  void Initialize(size_t max_data_len, size_t le_max_data_len, size_t max_num_packets,
                  size_t le_max_num_packets);

  // Unregisters event handlers and cleans up.
  // NOTE: Initialize() and ShutDown() MUST be called on the same thread. These methods are not
  // thread-safe.
  void ShutDown();

  // Returns the maximum length (in octets) of the data portion of each HCI ACL Data Packet that the
  // Controller is able to accept to be sent over a BR/EDR link.
  size_t GetMaxDataLength() const;

  // Returns the maximum length (in octets) of the data portion of each HCI ACL Data Packet that the
  // Controller is able to accept to be sent over a LE link.
  size_t GetLEMaxDataLength() const;

  // Returns the total number of HCI ACL Data Packets that can be stored in the data buffers of the
  // Controller for BR/EDR.
  size_t GetMaxNumberOfPackets() const;

  // Returns the total number of HCI ACL Data Packets that can be stored in the data buffers of the
  // Controller for LE.
  size_t GetLEMaxNumberOfPackets() const;

  // Queues the given ACL data packet to be sent to the controller. Returns false if the packet
  // cannot be queued up, e.g. if |data_packet| does not correspond to a known link layer
  // connection.
  //
  // |data_packet| is passed by value, meaning that ACLDataChannel will take ownership of it.
  // |data_packet| must represent a valid ACL data packet.
  bool SendPacket(common::DynamicByteBuffer data_packet);

  // Returns the underlying channel handle.
  const mx::channel& channel() const { return channel_; }

 private:
  // Represents a queued ACL data packet.
  struct QueuedDataPacket {
    QueuedDataPacket() = default;
    QueuedDataPacket(QueuedDataPacket&& other) = default;
    QueuedDataPacket& operator=(QueuedDataPacket&& other) = default;

    // TODO(armansito): We will need a better memory management scheme since copying packet data for
    // each data packet is going to cause huge performance issues. For now this is OK since we are
    // initially only supporting fixed-channel LE L2CAP.
    common::DynamicByteBuffer bytes;
  };

  // Handler for the HCI Number of Completed Packets Event, used for packet-based data flow control.
  void NumberOfCompletedPacketsCallback(const EventPacket& event);

  // Tries to send the next batch of queued data packets if the controller has any space available.
  void TrySendNextQueuedPackets();

  // Returns the number of BR/EDR packets for which the controller has available space to buffer.
  size_t GetNumFreeBREDRPacketsLocked() const;

  // Returns the number of LE packets for which controller has available space to buffer. Must be
  // called from a locked context.
  size_t GetNumFreeLEPacketsLocked() const;

  // Decreases the total number of sent packets count by the given amount. Must be called from a
  // locked context.
  void DecrementTotalNumPacketsLocked(size_t count);

  // Decreases the total number of sent packets count for LE by the given amount. Must be called
  // from a locked context.
  void DecrementLETotalNumPacketsLocked(size_t count);

  // Increments the total number of sent packets count by the given amount. Must be called from a
  // locked context.
  void IncrementTotalNumPacketsLocked(size_t count);

  // Increments the total number of sent LE packets count by the given amount. Must be called from a
  // locked context.
  void IncrementLETotalNumPacketsLocked(size_t count);

  // Returns true if the maximum number of sent packets has been reached. Must be called from a
  // locked context.
  bool MaxNumPacketsReachedLocked() const;

  // Returns true if the maximum number of sent LE packets has been reached. Must be called from a
  // locked context.
  bool MaxLENumPacketsReachedLocked() const;

  // ::mtl::MessageLoopHandler overrides:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override;
  void OnHandleError(mx_handle_t handle, mx_status_t error) override;

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
  DataReceivedCallback rx_callback_;
  ftl::RefPtr<ftl::TaskRunner> rx_task_runner_;

  // The buffer we use to temporarily write incoming data packets.
  // TODO(armansito): It might be better to initialize this dynamically based on the MTU reported by
  // the controller.
  common::StaticByteBuffer<ACLDataTxPacket::GetMinBufferSize(kMaxACLPayloadSize)> rx_buffer_;

  // Maximum length (in octets) of the data portion of each HCI ACL Data Packet that the Controller
  // is able to accept. If the controller maintains separate buffers for LE and BR/EDR then each
  // value will be non-zero. If the controller maintains a single buffer for both, then
  // |le_max_acl_data_len_| will be zero.
  size_t max_data_len_;
  size_t le_max_data_len_;

  // Total number of HCI ACL Data Packets that can be stored in the data buffers of the Controller.
  // |le_max_num_packets_| will be non-zero if the controller maintains separate buffers for LE
  // and BR/EDR.
  size_t max_num_packets_;
  size_t le_max_num_packets_;

  // Mutex that guards access to data transmission related members below.
  std::mutex send_mutex_;

  // The current count of the number of ACL data packets that have been sent to the controller.
  // |le_num_sent_packets_| is ignored if the controller uses one buffer for LE and BR/EDR.
  size_t num_sent_packets_;
  size_t le_num_sent_packets_;

  // The ACL data packet queue contains the data packets that are waiting to be sent to the
  // controller.
  // TODO(armansito): We'll probably need a smarter priority queue here since a simple FIFO queue
  // can cause a single connection to starve others when there are multiple connections present. We
  // should instead manage data packet priority based on its connection handle and on the result of
  // the HCI Number of Completed Packets events.
  std::queue<QueuedDataPacket> send_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ACLDataChannel);
};

}  // namespace hci
}  // namespace bluetooth
