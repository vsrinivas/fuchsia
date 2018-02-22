// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "channel_manager.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "logical_link.h"

namespace btlib {
namespace l2cap {

ChannelManager::ChannelManager(fxl::RefPtr<hci::Transport> hci,
                               fxl::RefPtr<fxl::TaskRunner> task_runner)
    : hci_(hci), task_runner_(task_runner), weak_ptr_factory_(this) {
  FXL_DCHECK(hci_);
  FXL_DCHECK(task_runner_);
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());

  // TODO(armansito): NET-353
  auto self = weak_ptr_factory_.GetWeakPtr();
  auto acl_handler = [self](auto pkt) {
    if (self) {
      self->OnACLDataReceived(std::move(pkt));
    }
  };
  hci_->acl_data_channel()->SetDataRxHandler(std::move(acl_handler),
                                             task_runner_);
}

ChannelManager::~ChannelManager() {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  hci_->acl_data_channel()->SetDataRxHandler({});
}

void ChannelManager::Register(hci::ConnectionHandle handle,
                              hci::Connection::LinkType ll_type,
                              hci::Connection::Role role) {
  RegisterInternal(handle, ll_type, role);
}

void ChannelManager::RegisterLE(
    hci::ConnectionHandle handle,
    hci::Connection::Role role,
    LEConnectionParameterUpdateCallback conn_param_cb,
    LinkErrorCallback link_error_cb,
    fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  FXL_VLOG(1) << "l2cap: register LE link (handle: " << handle << ")";

  auto* ll = RegisterInternal(handle, hci::Connection::LinkType::kLE, role);
  ll->set_error_callback(std::move(link_error_cb), task_runner);
  ll->le_signaling_channel()->set_conn_param_update_callback(conn_param_cb,
                                                             task_runner);
}

void ChannelManager::Unregister(hci::ConnectionHandle handle) {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());

  FXL_VLOG(1) << "l2cap: unregister LE link (handle: " << handle << ")";

  pending_packets_.erase(handle);
  auto count = ll_map_.erase(handle);
  FXL_DCHECK(count) << fxl::StringPrintf(
      "l2cap: Attempted to remove unknown connection handle: 0x%04x", handle);
}

fbl::RefPtr<Channel> ChannelManager::OpenFixedChannel(
    hci::ConnectionHandle handle,
    ChannelId channel_id) {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());

  auto iter = ll_map_.find(handle);
  if (iter == ll_map_.end()) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "l2cap: Cannot open fixed channel on unknown connection handle: 0x%04x",
        handle);
    return nullptr;
  }

  return iter->second->OpenFixedChannel(channel_id);
}

void ChannelManager::OnACLDataReceived(hci::ACLDataPacketPtr packet) {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());

  // TODO(armansito): Route packets based on channel priority, prioritizing
  // Guaranteed channels over Best Effort. Right now all channels are Best
  // Effort.

  auto handle = packet->connection_handle();

  auto iter = ll_map_.find(handle);
  PendingPacketMap::iterator pp_iter;

  // If a LogicalLink does not exist, we set up a queue for its packets to be
  // delivered when the LogicalLink gets created.
  if (iter == ll_map_.end()) {
    pp_iter = pending_packets_
                  .emplace(handle, common::LinkedList<hci::ACLDataPacket>())
                  .first;
  } else {
    // A logical link exists. |pp_iter| will be valid only if the drain task has
    // not run yet (see ChannelManager::RegisterInternal()).
    pp_iter = pending_packets_.find(handle);
  }

  if (pp_iter != pending_packets_.end()) {
    pp_iter->second.push_back(std::move(packet));

    FXL_VLOG(2) << fxl::StringPrintf(
        "l2cap: Queued rx packet on handle: 0x%04x", handle);
    return;
  }

  iter->second->HandleRxPacket(std::move(packet));
}

internal::LogicalLink* ChannelManager::RegisterInternal(
    hci::ConnectionHandle handle,
    hci::Connection::LinkType ll_type,
    hci::Connection::Role role) {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());

  auto iter = ll_map_.find(handle);
  FXL_DCHECK(iter == ll_map_.end()) << fxl::StringPrintf(
      "l2cap: Connection handle re-used! (handle=0x%04x)", handle);

  auto ll = std::make_unique<internal::LogicalLink>(handle, ll_type, role,
                                                    task_runner_, hci_);

  // Route all pending packets to the link.
  auto pp_iter = pending_packets_.find(handle);
  if (pp_iter != pending_packets_.end()) {
    auto& packets = pp_iter->second;
    while (!packets.is_empty()) {
      ll->HandleRxPacket(packets.pop_front());
    }
    pending_packets_.erase(pp_iter);
  }

  auto* ll_raw = ll.get();
  ll_map_[handle] = std::move(ll);

  return ll_raw;
}

}  // namespace l2cap
}  // namespace btlib
