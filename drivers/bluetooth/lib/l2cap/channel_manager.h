// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include <zircon/compiler.h>

#include "apps/bluetooth/lib/common/cancelable_callback.h"
#include "apps/bluetooth/lib/hci/acl_data_packet.h"
#include "apps/bluetooth/lib/hci/connection.h"
#include "apps/bluetooth/lib/hci/hci.h"
#include "apps/bluetooth/lib/l2cap/channel.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/synchronization/thread_checker.h"
#include "lib/fxl/tasks/task_runner.h"

namespace bluetooth {

namespace hci {
class Transport;
}  // namespace hci

namespace l2cap {

namespace internal {
class LogicalLink;
}  // namespace internal

// ChannelManager implements the "Channel Manager" control block of L2CAP. In particular:
//
//   * It acts as a routing table for incoming ACL data by directing packets to the appropriate
//   internal logical link handler;
//
//   * Manages priority based scheduling.
//
//   * Provides an API surface for L2CAP channel creation and logical link management bound to a
//   single creation thread.
//
// There is a single instance of ChannelManager for each HCI transport.
class ChannelManager final {
 public:
  ChannelManager(fxl::RefPtr<hci::Transport> hci, fxl::RefPtr<fxl::TaskRunner> task_runner);
  ~ChannelManager();

  // Registers the given connection with the L2CAP layer. L2CAP channels can be opened on the
  // logical link represented by |handle| after a call to this method.
  //
  // It is an error to register the same |handle| value more than once without first unregistering
  // it (this is asserted in debug builds).
  void Register(hci::ConnectionHandle handle, hci::Connection::LinkType ll_type,
                hci::Connection::Role role);

  // Removes a previously registered connection. All corresponding Channels will be closed and all
  // incoming data packets on this link will be dropped.
  //
  // NOTE: It is recommended that a link entry be removed AFTER the controller sends a HCI
  // Disconnection Complete Event for the corresponding logical link. This is to prevent incorrectly
  // buffering data if the controller has more packets to send after removing the link entry.
  void Unregister(hci::ConnectionHandle handle);

  // Opens the L2CAP fixed channel with |channel_id| over the logical link identified by
  // |connection_handle| and starts routing packets. Returns nullptr if the channel is already open.
  //
  // See channel.h for documentation on |rx_callback| and |closed_callback|. |rx_callback| is always
  // posted on |rx_task_runner|. |closed_callback| always runs on the thread that created this
  // ChannelManager.
  std::unique_ptr<Channel> OpenFixedChannel(hci::ConnectionHandle connection_handle,
                                            ChannelId channel_id);

 private:
  // Called when an ACL data packet is received from the controller. This method is responsible for
  // routing the packet to the corresponding LogicalLink.
  void OnACLDataReceived(hci::ACLDataPacketPtr data_packet);

  fxl::RefPtr<hci::Transport> hci_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  std::mutex mtx_;
  std::unordered_map<hci::ConnectionHandle, std::unique_ptr<internal::LogicalLink>> ll_map_
      __TA_GUARDED(mtx_);

  // Stores packets received on a connection handle before a link for it has been created.
  using PendingPacketMap =
      std::unordered_map<hci::ConnectionHandle, fbl::DoublyLinkedList<hci::ACLDataPacketPtr>>;
  PendingPacketMap pending_packets_ __TA_GUARDED(mtx_);

  common::CancelableCallbackFactory<void()> cancelable_callback_factory_;
  fxl::ThreadChecker thread_checker_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ChannelManager);
};

}  // namespace l2cap
}  // namespace bluetooth
