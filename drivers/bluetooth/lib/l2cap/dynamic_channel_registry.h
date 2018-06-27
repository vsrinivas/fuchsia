// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_DYNAMIC_CHANNEL_REGISTRY_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_DYNAMIC_CHANNEL_REGISTRY_H_

#include <unordered_map>

#include <lib/fit/function.h>

#include "garnet/drivers/bluetooth/lib/l2cap/dynamic_channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/signaling_channel.h"

namespace btlib {
namespace l2cap {
namespace internal {

// Base class for registries of dynamic L2CAP channels. It serves both as the
// factory and owner of dynamic channels created on a logical link, including
// assigning and tracking dynamic channel IDs.
//
// Registry entries are DynamicChannels that do not implement the l2cap::Channel
// interface used for user data transfer.
//
// This class is not thread-safe and is intended to be created and run on the
// L2CAP thread for each logical link connected.
class DynamicChannelRegistry {
 public:
  // Used to pass an optional channel to clients of the registry. |channel| may
  // be nullptr upon failure to open. Otherwise, it points to an instance owned
  // by the registry and should not be retained by the callee.
  using DynamicChannelCallback =
      fit::function<void(const DynamicChannel* channel)>;

  virtual ~DynamicChannelRegistry();

  // Create and connect a dynamic channel. The result will be returned by
  // calling |open_cb| on the L2CAP thread the channel is ready for data
  // transfer, with a nullptr if unsuccessful. The DynamicChannel passed will
  // contain the local and remote channel IDs to be used for user data transfer
  // over the new channel.
  void OpenOutbound(PSM psm, DynamicChannelCallback open_cb);

  // Disconnect and remove the channel identified by |local_cid|. After this
  // call completes, incoming PDUs with |local_cid| should be discarded as in
  // error or considered to belong to a subsequent channel with that ID. Any
  // outbound PDUs passed to the Channel interface for this channel should be
  // discarded. The internal channel will be immediately destroyed and
  // |local_cid| may be recycled for another dynamic channel.
  //
  // TODO(xow): Maybe take a DynamicChannel* to have greater confidence over the
  // instance of DynamicChannel being deleted (similar to
  // |LogicalLink::RemoveChannel|)?
  void CloseChannel(ChannelId local_cid);

 protected:
  // |largest_channel_id| is the greatest dynamic channel ID that can be
  // allocated on this link and must be at least |kFirstDynamicChannelId|.
  //
  // |close_cb| will be called upon a remote-initiated closure of an open
  // channel. The registry's internal channel is passed as a parameter, and it
  // will be closed for user data transfer before the callback fires. When the
  // callback returns, the channel is destroyed and its ID may be recycled for
  // another dynamic channel. Channels that fail to open due to error or are
  // closed using CloseChannel will not trigger this callback.
  //
  // TODO(NET-1135): Assign an inbound channel callback
  DynamicChannelRegistry(ChannelId largest_channel_id_,
                         DynamicChannelCallback close_cb);

  // Factory method for a DynamicChannel implementation that represents an
  // outbound channel with an endpoint on this device identified by |local_cid|.
  virtual DynamicChannelPtr MakeOutbound(PSM psm, ChannelId local_cid) = 0;

  // Starting at kFirstDynamicChannelId and ending on |largest_channel_id_|
  // (inclusive), search for the next dynamic channel ID available on this link.
  // Returns kInvalidChannelId if all IDs have been exhausted.
  ChannelId FindAvailableChannelId() const;

  // Returns null if not found. Can be downcast to the derived DynamicChannel
  // created by MakeOutbound.
  DynamicChannel* FindChannel(ChannelId local_cid) const;

 private:
  friend class DynamicChannel;

  // Open a newly-created channel and invoke |open_cb| with the result of the
  // operation, which may be nullptr if the channel failed to open.
  void ActivateChannel(DynamicChannel* channel, DynamicChannelCallback open_cb);

  // Signal a remote-initiated closure of a channel owned by this registry, then
  // delete it. |close_cb_| is invoked if the channel was ever open (see
  // |DynamicChannel::opened|).
  void OnChannelDisconnected(DynamicChannel* channel);

  // Delete a channel owned by this registry. Then, after this returns,
  // |local_cid| may be recycled for another dynamic channel.
  void RemoveChannel(DynamicChannel* channel);

  // Greatest dynamic channel ID that can be assigned on the kind of logical
  // link associated to this registry.
  const ChannelId largest_channel_id_;

  // Called only for channels that were already open (see
  // |DynamicChannel::opened|).
  DynamicChannelCallback close_cb_;

  // Maps local CIDs to alive dynamic channels on this logical link.
  std::unordered_map<ChannelId, DynamicChannelPtr> channels_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DynamicChannelRegistry);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_DYNAMIC_CHANNEL_REGISTRY_H_
