// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <magenta/compiler.h>

#include "apps/bluetooth/lib/common/cancelable_callback.h"
#include "apps/bluetooth/lib/hci/acl_data_packet.h"
#include "apps/bluetooth/lib/hci/connection.h"
#include "apps/bluetooth/lib/hci/hci.h"
#include "apps/bluetooth/lib/hci/transport.h"
#include "apps/bluetooth/lib/l2cap/channel.h"
#include "apps/bluetooth/lib/l2cap/fragmenter.h"
#include "apps/bluetooth/lib/l2cap/l2cap.h"
#include "apps/bluetooth/lib/l2cap/recombiner.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/synchronization/thread_checker.h"
#include "lib/fxl/tasks/task_runner.h"

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
              hci::Connection::Role role, fxl::RefPtr<hci::Transport> hci);

  // When a logical link is destroyed it notifies all of its channels to close themselves. Data
  // packets will no longer be routed to the associated channels.
  ~LogicalLink();

  // Opens the channel with |channel_id| over this logical link. See channel.h for documentation on
  // |rx_callback| and |closed_callback|. Returns nullptr if a Channel for |channel_id| already
  // exists.
  std::unique_ptr<Channel> OpenFixedChannel(ChannelId channel_id);

  // Takes ownership of |packet| for PDU processing and routes it to its target channel. This must
  // be called on the HCI I/O thread.
  void HandleRxPacket(hci::ACLDataPacketPtr packet);

  // Sends a B-frame PDU out over the ACL data channel, where |payload| is the B-frame information
  // payload. |id| identifies the L2CAP channel that this frame is coming from. This must be called
  // on the HCI I/O thread.
  void SendBasicFrame(ChannelId id, const common::ByteBuffer& payload);

  // Returns the HCI I/O thread task runner.
  fxl::RefPtr<fxl::TaskRunner> io_task_runner() const { return hci_->io_task_runner(); }

  hci::Connection::LinkType type() const { return type_; }

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

  fxl::RefPtr<hci::Transport> hci_;

  // Information about the underlying controller logical link.
  hci::ConnectionHandle handle_;
  hci::Connection::LinkType type_;
  hci::Connection::Role role_;

  // TODO(armansito): Store a signaling channel implementation separately from other fixed channels.

  // Fragmenter and Recombiner should always be accessed on the HCI I/O thread.
  Fragmenter fragmenter_;
  Recombiner recombiner_;

  std::mutex mtx_;

  // LogicalLink stores raw pointers to its channels. Each Channel notifies its link when it is
  // about to be destroyed to prevent use-after-free.
  using ChannelMap = std::unordered_map<ChannelId, ChannelImpl*>;
  ChannelMap channels_ __TA_GUARDED(mtx_);

  // Stores packets that have been received on a currently closed channel. We buffer these for fixed
  // channels so that the data is available when the channel is opened.
  using PendingPduMap = std::unordered_map<ChannelId, std::list<PDU>>;
  PendingPduMap pending_pdus_ __TA_GUARDED(mtx_);

  common::CancelableCallbackFactory<void()> cancelable_callback_factory_;
  fxl::ThreadChecker thread_checker_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LogicalLink);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace bluetooth
