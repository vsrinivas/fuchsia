// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include <lib/async/dispatcher.h>
#include <zircon/compiler.h>

#include "garnet/drivers/bluetooth/lib/hci/acl_data_packet.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/le_signaling_channel.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_checker.h"

namespace btlib {

namespace hci {
class Transport;
}  // namespace hci

namespace l2cap {

namespace internal {
class LogicalLink;
}  // namespace internal

// ChannelManager implements the "Channel Manager" control block of L2CAP. In
// particular:
//
//   * It acts as a routing table for incoming ACL data by directing packets to
//     the appropriate internal logical link handler;
//
//   * Manages priority based scheduling.
//
//   * Provides an API surface for L2CAP channel creation and logical link
//     management bound to a single creation thread.
//
// There can be a single instance of ChannelManager for a HCI transport.
//
// THREAD-SAFETY:
//
// This object is not thread safe. Construction/destruction must happen on the
// thread where this is used.
class ChannelManager final {
 public:
  using LinkErrorCallback = fit::closure;

  ChannelManager(fxl::RefPtr<hci::Transport> hci, async_dispatcher_t* l2cap_dispatcher);
  ~ChannelManager();

  // Registers the ACL connection with the L2CAP layer. L2CAP channels can be
  // opened on the logical link represented by |handle| after a call to this
  // method.
  //
  // |link_error_callback| will be used to notify when a channel signals a link
  // error. It will be posted onto |dispatcher|.
  //
  // It is an error to register the same |handle| value more than once as either
  // kind of channel without first unregistering it (asserted in debug builds).
  void RegisterACL(hci::ConnectionHandle handle,
                   hci::Connection::Role role,
                   LinkErrorCallback link_error_callback,
                   async_dispatcher_t* dispatcher);

  // Registers a LE connection with the L2CAP layer. L2CAP channels can be
  // opened on the logical link represented by |handle| after a call to this
  // method.
  //
  // |conn_param_callback| will be used to notify the caller if new connection
  // parameters were accepted from the remote end of the link.
  //
  // |link_error_callback| will be used to notify when a channel signals a link
  // error.
  //
  // Both callbacks will be posted onto |dispatcher|.
  //
  // It is an error to register the same |handle| value more than once as either
  // kind of channel without first unregistering it (asserted in debug builds).
  using LEConnectionParameterUpdateCallback =
      internal::LESignalingChannel::ConnectionParameterUpdateCallback;
  void RegisterLE(hci::ConnectionHandle handle,
                  hci::Connection::Role role,
                  LEConnectionParameterUpdateCallback conn_param_callback,
                  LinkErrorCallback link_error_callback,
                  async_dispatcher_t* dispatcher);

  // Removes a previously registered connection. All corresponding Channels will
  // be closed and all incoming data packets on this link will be dropped.
  //
  // NOTE: It is recommended that a link entry be removed AFTER the controller
  // sends a HCI Disconnection Complete Event for the corresponding logical
  // link. This is to prevent incorrectly buffering data if the controller has
  // more packets to send after removing the link entry.
  void Unregister(hci::ConnectionHandle handle);

  // Opens the L2CAP fixed channel with |channel_id| over the logical link
  // identified by |connection_handle| and starts routing packets. Returns
  // nullptr if the channel is already open.
  fbl::RefPtr<Channel> OpenFixedChannel(hci::ConnectionHandle connection_handle,
                                        ChannelId channel_id);

 private:
  // Called when an ACL data packet is received from the controller. This method
  // is responsible for routing the packet to the corresponding LogicalLink.
  void OnACLDataReceived(hci::ACLDataPacketPtr data_packet);

  // Called by the various Register functions. Returns a pointer to the newly
  // added link.
  internal::LogicalLink* RegisterInternal(hci::ConnectionHandle handle,
                                          hci::Connection::LinkType ll_type,
                                          hci::Connection::Role role);

  fxl::RefPtr<hci::Transport> hci_;
  async_dispatcher_t* l2cap_dispatcher_;

  using LinkMap = std::unordered_map<hci::ConnectionHandle,
                                     std::unique_ptr<internal::LogicalLink>>;
  LinkMap ll_map_;

  // Stores packets received on a connection handle before a link for it has
  // been created.
  using PendingPacketMap =
      std::unordered_map<hci::ConnectionHandle,
                         common::LinkedList<hci::ACLDataPacket>>;
  PendingPacketMap pending_packets_;

  fxl::ThreadChecker thread_checker_;
  fxl::WeakPtrFactory<ChannelManager> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ChannelManager);
};

}  // namespace l2cap
}  // namespace btlib
