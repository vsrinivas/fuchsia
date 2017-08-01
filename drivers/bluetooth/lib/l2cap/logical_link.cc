// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logical_link.h"

#include "lib/ftl/logging.h"

#include "channel.h"

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
  Close();
}

std::unique_ptr<Channel> LogicalLink::OpenFixedChannel(ChannelId id) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  // We currently only support the pre-defined fixed-channels.
  if (!AllowsFixedChannel(id)) {
    FTL_LOG(ERROR) << "l2cap: Cannot open fixed channel with id: 0x" << std::hex << id;
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = channels_.find(id);

  if (iter != channels_.end()) {
    FTL_LOG(ERROR) << "l2cap: Channel is already open! (handle: 0x" << std::hex << handle_
                   << " channel-id: 0x" << id;
    return nullptr;
  }

  auto chan = new ChannelImpl(id, this);

  // We grab a raw pointer and store it internally, weakly owning all channels that we create. We
  // avoid dangling pointers by relying on the fact that each Channel notifies on destruction by
  // calling RemoveChannel().
  channels_[id] = chan;

  return std::unique_ptr<Channel>(chan);
}

void LogicalLink::HandleRxPacket(std::unique_ptr<hci::ACLDataPacket> packet) {
  // The creation thread of this object is expected to be different from the HCI I/O thread.
  FTL_DCHECK(!thread_checker_.IsCreationThreadCurrent());

  // TODO(armansito): implement
}

bool LogicalLink::AllowsFixedChannel(ChannelId id) {
  return (type_ == hci::Connection::LinkType::kLE) ? IsValidLEFixedChannel(id)
                                                   : IsValidBREDRFixedChannel(id);
}

void LogicalLink::RemoveChannel(internal::ChannelImpl* channel) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FTL_DCHECK(channel);

  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = channels_.find(channel->id());

  // This could happen in a race-condition where Close() is called concurrently with |channel|'s
  // destructor.
  if (iter == channels_.end()) return;
  FTL_DCHECK(iter->first == channel->id());

  channels_.erase(iter);
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
