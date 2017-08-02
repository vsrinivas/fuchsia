// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include <magenta/compiler.h>

#include "apps/bluetooth/lib/hci/acl_data_packet.h"
#include "apps/bluetooth/lib/hci/connection.h"
#include "apps/bluetooth/lib/hci/hci.h"
#include "apps/bluetooth/lib/l2cap/channel.h"
#include "apps/bluetooth/lib/l2cap/l2cap.h"
#include "apps/bluetooth/lib/l2cap/recombiner.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/synchronization/thread_checker.h"

namespace bluetooth {
namespace l2cap {

class ChannelManager;

namespace internal {

class ChannelImpl;

// Represents a controller logical link. Each instance aids in mapping L2CAP channels to their
// corresponding controller logical link and vice versa. Instances are created and owned by a
// ChannelManager.
class LogicalLink final {
 public:
  LogicalLink(hci::ConnectionHandle handle, hci::Connection::LinkType type,
              hci::Connection::Role role, ChannelManager* owner);

  // When a logical link is destroyed it notifies all of its channels to close themselves. Data
  // packets will no longer be routed to the associated channels.
  ~LogicalLink();

  // Opens the channel with |channel_id| over this logical link. See channel.h for documentation on
  // |rx_callback| and |closed_callback|. Returns nullptr if a Channel for |channel_id| already
  // exists.
  std::unique_ptr<Channel> OpenFixedChannel(ChannelId channel_id);

  // Takes ownership of |packet| for PDU processing and routes it to its target channel.
  void HandleRxPacket(hci::ACLDataPacketPtr packet);

 private:
  friend class ChannelImpl;

  bool AllowsFixedChannel(ChannelId id);

  // Called by an open ChannelImpl when it is about to be destroyed. Removes the entry from the
  // channel map.
  //
  // This is the only internal member that is accessed by ChannelImpl. This MUST NOT call any of the
  // locking methods of |channel| to prevent a deadlock.
  void RemoveChannel(ChannelImpl* channel);

  // Notifies and closes all open channels on this link. Called by the destructor.
  void Close();

  // The ChannelManager that uniquely owns this instance.
  ChannelManager* owner_;  // weak

  // Information about the underlying controller logical link.
  hci::ConnectionHandle handle_;
  hci::Connection::LinkType type_;
  hci::Connection::Role role_;

  // TODO(armansito): Store a signaling channel implementation separately from other fixed channels.

  std::mutex mtx_;

  // LogicalLink stores raw pointers to its channels. Each Channel notifies its link when it is
  // about to be destroyed to prevent use-after-free.
  using ChannelMap = std::unordered_map<ChannelId, ChannelImpl*>;
  ChannelMap channels_ __TA_GUARDED(mtx_);

  ftl::ThreadChecker thread_checker_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LogicalLink);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace bluetooth
