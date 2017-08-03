// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logical_link.h"

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

#include "channel.h"
#include "channel_manager.h"

namespace bluetooth {
namespace l2cap {
namespace internal {
namespace {

constexpr bool IsValidLEFixedChannel(ChannelId id) {
  switch (id) {
    case kATTChannelId:
    case kLESignalingChannelId:
    case kSMPChannelId:
      return true;
    default:
      break;
  }
  return false;
}

constexpr bool IsValidBREDRFixedChannel(ChannelId id) {
  switch (id) {
    case kSignalingChannelId:
    case kConnectionlessChannelId:
    case kSMChannelId:
      return true;
    default:
      break;
  }
  return false;
}

}  // namespace

LogicalLink::LogicalLink(hci::ConnectionHandle handle, hci::Connection::LinkType type,
                         hci::Connection::Role role, ChannelManager* owner)
    : owner_(owner), handle_(handle), type_(type), role_(role) {
  FTL_DCHECK(owner_);
  FTL_DCHECK(type_ == hci::Connection::LinkType::kLE || type_ == hci::Connection::LinkType::kACL);
}

LogicalLink::~LogicalLink() {
  cancelable_callback_factory_.CancelAll();
  Close();
}

std::unique_ptr<Channel> LogicalLink::OpenFixedChannel(ChannelId id) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  // We currently only support the pre-defined fixed-channels.
  if (!AllowsFixedChannel(id)) {
    FTL_LOG(ERROR) << ftl::StringPrintf("l2cap: Cannot open fixed channel with id 0x%04x", id);
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = channels_.find(id);

  if (iter != channels_.end()) {
    FTL_LOG(ERROR) << ftl::StringPrintf(
        "l2cap: Channel is already open! (id: 0x%04x, handle: 0x%04x)", id, handle_);
    return nullptr;
  }

  auto chan = std::unique_ptr<ChannelImpl>(new ChannelImpl(id, this));

  // We grab a raw pointer and store it internally, weakly owning all channels that we create. We
  // avoid dangling pointers by relying on the fact that each Channel notifies on destruction by
  // calling RemoveChannel().
  channels_[id] = chan.get();

  // Handle pending packets on the channel, if any.
  auto pp_iter = pending_pdus_.find(id);
  if (pp_iter != pending_pdus_.end()) {
    owner_->io_task_runner()->PostTask(cancelable_callback_factory_.MakeTask([this, id] {
      std::lock_guard<std::mutex> lock(mtx_);

      // Make sure the channel is still open.
      auto iter = channels_.find(id);
      if (iter == channels_.end()) return;

      auto pp_iter = pending_pdus_.find(id);
      FTL_DCHECK(pp_iter != pending_pdus_.end());

      auto chan = iter->second;
      auto& pdus = pp_iter->second;
      while (!pdus.empty()) {
        chan->HandleRxPdu(std::move(pdus.front()));
        pdus.pop_front();
      }
      pending_pdus_.erase(pp_iter);
    }));
  }

  return chan;
}

void LogicalLink::HandleRxPacket(hci::ACLDataPacketPtr packet) {
  // The creation thread of this object is expected to be different from the HCI I/O thread.
  FTL_DCHECK(!thread_checker_.IsCreationThreadCurrent());
  FTL_DCHECK(!recombiner_.ready());
  FTL_DCHECK(packet);

  if (!recombiner_.AddFragment(std::move(packet))) {
    FTL_VLOG(1) << ftl::StringPrintf("l2cap: ACL data packet rejected (handle: 0x%04x)", handle_);

    // TODO(armansito): This indicates that this connection is not reliable. This needs to notify
    // the channels of this state.
    return;
  }

  // |recombiner_| should have taken ownership of |packet|.
  FTL_DCHECK(!packet);
  FTL_DCHECK(!recombiner_.empty());

  // Wait for continuation fragments if a partial fragment was received.
  if (!recombiner_.ready()) return;

  PDU pdu;
  recombiner_.Release(&pdu);

  FTL_DCHECK(pdu.is_valid());

  std::lock_guard<std::mutex> lock(mtx_);

  uint16_t channel_id = pdu.channel_id();
  auto iter = channels_.find(channel_id);
  PendingPduMap::iterator pp_iter;

  // TODO(armansito): This buffering scheme could be problematic for dynamically negotiated
  // channels if a channel id were to be recycled, as it requires careful management of the timing
  // between channel destruction and data buffering. Probably only buffer data for fixed channels?

  // If a ChannelImpl does not exist, we buffer its packets to be delivered later. If one DOES
  // exist, we buffer it only if the task to drain the pending packets has not yet run. (See
  // LogicalLink::OpenFixedChannel above).
  if (iter == channels_.end()) {
    // The packet was received on a channel for which no ChannelImpl currently exists.
    // Buffer packets for the channel to receive when it gets created.
    pp_iter = pending_pdus_.emplace(channel_id, std::list<PDU>()).first;
  } else {
    pp_iter = pending_pdus_.find(channel_id);
  }

  if (pp_iter != pending_pdus_.end()) {
    pp_iter->second.emplace_back(std::move(pdu));

    FTL_VLOG(1) << ftl::StringPrintf("l2cap: PDU buffered (channel: 0x%04x, ll: 0x%04x", channel_id,
                                     handle_);
    return;
  }

  // Off it goes.
  iter->second->HandleRxPdu(std::move(pdu));
}

bool LogicalLink::AllowsFixedChannel(ChannelId id) {
  return (type_ == hci::Connection::LinkType::kLE) ? IsValidLEFixedChannel(id)
                                                   : IsValidBREDRFixedChannel(id);
}

void LogicalLink::RemoveChannel(ChannelImpl* channel) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FTL_DCHECK(channel);

  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = channels_.find(channel->id());
  FTL_DCHECK(iter != channels_.end()) << ftl::StringPrintf(
      "l2cap: Attempted to remove unknown channel (id: 0x%04x, handle: 0x%04x)", channel->id(),
      handle_);
  FTL_DCHECK(iter->first == channel->id());

  channels_.erase(iter);
  pending_pdus_.erase(channel->id());
}

void LogicalLink::Close() {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  ChannelMap channels;

  // Clear |channels_| before notifying each entry to avoid holding our |mtx_| while Channel's own
  // internal mutex is locked.
  {
    std::lock_guard<std::mutex> lock(mtx_);
    channels.swap(channels_);
  }

  for (auto& iter : channels) {
    iter.second->OnLinkClosed();
  }
}

}  // namespace internal
}  // namespace l2cap
}  // namespace bluetooth
