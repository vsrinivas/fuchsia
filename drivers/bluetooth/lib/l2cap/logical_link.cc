// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logical_link.h"

#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "bredr_signaling_channel.h"
#include "channel.h"
#include "le_signaling_channel.h"

namespace btlib {
namespace l2cap {
namespace internal {
namespace {

constexpr bool IsValidLEFixedChannel(ChannelId id) {
  switch (id) {
    case kATTChannelId:
    case kLESignalingChannelId:
    case kLESMPChannelId:
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
    case kSMPChannelId:
      return true;
    default:
      break;
  }
  return false;
}

}  // namespace

LogicalLink::LogicalLink(hci::ConnectionHandle handle,
                         hci::Connection::LinkType type,
                         hci::Connection::Role role, async_t* dispatcher,
                         fxl::RefPtr<hci::Transport> hci)
    : hci_(hci),
      dispatcher_(dispatcher),
      handle_(handle),
      type_(type),
      role_(role),
      fragmenter_(handle),
      weak_ptr_factory_(this) {
  FXL_DCHECK(hci_);
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(type_ == hci::Connection::LinkType::kLE ||
             type_ == hci::Connection::LinkType::kACL);

  if (type_ == hci::Connection::LinkType::kLE) {
    FXL_DCHECK(hci_->acl_data_channel()->GetLEBufferInfo().IsAvailable());
    fragmenter_.set_max_acl_payload_size(
        hci_->acl_data_channel()->GetLEBufferInfo().max_data_length());
  } else {
    FXL_DCHECK(hci_->acl_data_channel()->GetBufferInfo().IsAvailable());
    fragmenter_.set_max_acl_payload_size(
        hci_->acl_data_channel()->GetBufferInfo().max_data_length());
  }

  // Set up the signaling channel.
  if (type_ == hci::Connection::LinkType::kLE) {
    signaling_channel_ = std::make_unique<LESignalingChannel>(
        OpenFixedChannel(kLESignalingChannelId), role_);
  } else {
    signaling_channel_ = std::make_unique<BrEdrSignalingChannel>(
        OpenFixedChannel(kSignalingChannelId), role_);
  }
}

LogicalLink::~LogicalLink() { Close(); }

fbl::RefPtr<Channel> LogicalLink::OpenFixedChannel(ChannelId id) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  // We currently only support the pre-defined fixed-channels.
  if (!AllowsFixedChannel(id)) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "l2cap: Cannot open fixed channel with id 0x%04x", id);
    return nullptr;
  }

  auto iter = channels_.find(id);
  if (iter != channels_.end()) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "l2cap: Channel is already open! (id: 0x%04x, handle: 0x%04x)", id,
        handle_);
    return nullptr;
  }

  std::list<PDU> pending;
  auto pp_iter = pending_pdus_.find(id);
  if (pp_iter != pending_pdus_.end()) {
    pending = std::move(pp_iter->second);
    pending_pdus_.erase(pp_iter);
  }

  auto chan = fbl::AdoptRef(
      new ChannelImpl(id, weak_ptr_factory_.GetWeakPtr(), std::move(pending)));
  channels_[id] = chan;

  return chan;
}

void LogicalLink::HandleRxPacket(hci::ACLDataPacketPtr packet) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(!recombiner_.ready());
  FXL_DCHECK(packet);

  if (!recombiner_.AddFragment(std::move(packet))) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "l2cap: ACL data packet rejected (handle: 0x%04x)", handle_);

    // TODO(armansito): This indicates that this connection is not reliable.
    // This needs to notify the channels of this state.
    return;
  }

  // |recombiner_| should have taken ownership of |packet|.
  FXL_DCHECK(!packet);
  FXL_DCHECK(!recombiner_.empty());

  // Wait for continuation fragments if a partial fragment was received.
  if (!recombiner_.ready())
    return;

  PDU pdu;
  recombiner_.Release(&pdu);

  FXL_DCHECK(pdu.is_valid());

  uint16_t channel_id = pdu.channel_id();
  auto iter = channels_.find(channel_id);
  PendingPduMap::iterator pp_iter;

  // TODO(armansito): This buffering scheme could be problematic for dynamically
  // negotiated channels if a channel id were to be recycled, as it requires
  // careful management of the timing between channel destruction and data
  // buffering. Probably only buffer data for fixed channels?

  if (iter == channels_.end()) {
    // The packet was received on a channel for which no ChannelImpl currently
    // exists. Buffer packets for the channel to receive when it gets created.
    pp_iter = pending_pdus_.emplace(channel_id, std::list<PDU>()).first;
  } else {
    // A channel exists. |pp_iter| will be valid only if the drain task has not
    // run yet (see LogicalLink::OpenFixedChannel()).
    pp_iter = pending_pdus_.find(channel_id);
  }

  if (pp_iter != pending_pdus_.end()) {
    pp_iter->second.emplace_back(std::move(pdu));

    FXL_VLOG(2) << fxl::StringPrintf(
        "l2cap: PDU buffered (channel: 0x%04x, ll: 0x%04x)", channel_id,
        handle_);
    return;
  }

  iter->second->HandleRxPdu(std::move(pdu));
}

void LogicalLink::SendBasicFrame(ChannelId id,
                                 const common::ByteBuffer& payload) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  // TODO(armansito): The following makes a copy of |payload| when constructing
  // |pdu|. Think about how this could be optimized, especially when |payload|
  // fits inside a single ACL data fragment.
  PDU pdu = fragmenter_.BuildBasicFrame(id, payload);
  auto fragments = pdu.ReleaseFragments();

  FXL_DCHECK(!fragments.is_empty());
  hci_->acl_data_channel()->SendPackets(std::move(fragments), type_);
}

void LogicalLink::set_error_callback(fit::closure callback,
                                     async_t* dispatcher) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(static_cast<bool>(callback) == static_cast<bool>(dispatcher));

  link_error_cb_ = std::move(callback);
  link_error_dispatcher_ = dispatcher;
}

LESignalingChannel* LogicalLink::le_signaling_channel() const {
  return (type_ == hci::Connection::LinkType::kLE)
             ? static_cast<LESignalingChannel*>(signaling_channel_.get())
             : nullptr;
}

bool LogicalLink::AllowsFixedChannel(ChannelId id) {
  return (type_ == hci::Connection::LinkType::kLE)
             ? IsValidLEFixedChannel(id)
             : IsValidBREDRFixedChannel(id);
}

void LogicalLink::RemoveChannel(Channel* chan) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(chan);

  auto iter = channels_.find(chan->id());
  if (iter == channels_.end())
    return;

  // Ignore if the found channel doesn't match the requested one (even though
  // their IDs are the same).
  if (iter->second.get() != chan)
    return;

  pending_pdus_.erase(chan->id());
  channels_.erase(iter);
}

void LogicalLink::SignalError() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  if (link_error_cb_) {
    async::PostTask(link_error_dispatcher_, std::move(link_error_cb_));
    link_error_dispatcher_ = nullptr;
  }
}

void LogicalLink::Close() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  auto channels = std::move(channels_);
  for (auto& iter : channels) {
    static_cast<ChannelImpl*>(iter.second.get())->OnLinkClosed();
  }

  FXL_DCHECK(channels_.empty());
}

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
