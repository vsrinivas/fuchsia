// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "channel_manager.h"

#include "apps/bluetooth/lib/hci/transport.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

#include "logical_link.h"

namespace bluetooth {
namespace l2cap {

ChannelManager::ChannelManager(ftl::RefPtr<hci::Transport> hci,
                               ftl::RefPtr<ftl::TaskRunner> task_runner)
    : hci_(hci), task_runner_(task_runner) {
  FTL_DCHECK(hci_);
  FTL_DCHECK(task_runner_);

  hci_->acl_data_channel()->SetDataRxHandler(
      std::bind(&ChannelManager::OnACLDataReceived, this, std::placeholders::_1));
}

ChannelManager::~ChannelManager() {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  hci_->acl_data_channel()->SetDataRxHandler({});
}

void ChannelManager::Register(hci::ConnectionHandle handle, hci::Connection::LinkType ll_type,
                              hci::Connection::Role role) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = ll_map_.find(handle);
  FTL_DCHECK(iter == ll_map_.end())
      << ftl::StringPrintf("l2cap: Connection registered more than once! (handle=0x%04x)", handle);

  ll_map_[handle] = std::make_unique<internal::LogicalLink>(handle, ll_type, role, this);
}

void ChannelManager::Unregister(hci::ConnectionHandle handle) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = ll_map_.find(handle);
  FTL_DCHECK(iter != ll_map_.end())
      << "l2cap: Attempted to remove unknown connection handle: 0x" << std::hex << handle;
  ll_map_.erase(iter);
}

std::unique_ptr<Channel> ChannelManager::OpenFixedChannel(hci::ConnectionHandle connection_handle,
                                                          ChannelId channel_id) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = ll_map_.find(connection_handle);
  if (iter == ll_map_.end()) {
    FTL_LOG(ERROR) << ftl::StringPrintf(
        "l2cap: Cannot create channel on unknown connection handle: 0x%04x", connection_handle);
    return nullptr;
  }

  return iter->second->OpenFixedChannel(channel_id);
}

void ChannelManager::OnACLDataReceived(std::unique_ptr<hci::ACLDataPacket> packet) {
  // The creation thread of this object is expected to be different from the HCI I/O thread.
  FTL_DCHECK(!thread_checker_.IsCreationThreadCurrent());

  // TODO(armansito): Route packets based on channel priority, prioritizing Guaranteed channels over
  // Best Effort. Right now all channels are Best Effort.

  auto handle = packet->connection_handle();

  std::lock_guard<std::mutex> lock(mtx_);
  auto iter = ll_map_.find(handle);
  if (iter == ll_map_.end()) {
    // TODO(armansito): Buffer packets internally to prevent dropping packets before their target
    // Channel and LogicalLink get created. This is a potential race condition during connection set
    // up.
    FTL_VLOG(2) << ftl::StringPrintf("l2cap: Dropping packet on connection: 0x%04x", handle);
    return;
  }

  // NOTE: |mtx_| will remain locked until the packet is pushed over to the channel's own data
  // handler. It is important that LogicalLink make no calls to ChannelManager's public methods in
  // this context.
  // TODO(armansito): We should improve this once we support L2CAP modes other than basic mode and
  // if we add more threads for data handling. This can be especially problematic if the target
  // Channel's mode implementation does any long-running computation, which would cause the thread
  // calling Register/Unregister to potentially block for a long time (not to mention data coming in
  // over other threads, if we add them).
  // TODO(armansito): It's probably OK to keep shared_ptrs to LogicalLink and to temporarily add a
  // ref before calling HandleRxPacket on it. Revisit later.
  iter->second->HandleRxPacket(std::move(packet));
}

}  // namespace l2cap
}  // namespace bluetooth
