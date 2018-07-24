// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_layer.h"

#include <lib/async/cpp/task.h>

#include "fake_channel.h"

namespace btlib {
namespace l2cap {
namespace testing {

void FakeLayer::TriggerLEConnectionParameterUpdate(
    hci::ConnectionHandle handle,
    const hci::LEPreferredConnectionParameters& params) {
  FXL_DCHECK(initialized_);

  LinkData& link_data = FindLinkData(handle);
  async::PostTask(
      link_data.dispatcher,
      [params, cb = link_data.le_conn_param_cb.share()] { cb(params); });
}

void FakeLayer::TriggerOutboundChannel(hci::ConnectionHandle handle, PSM psm,
                                       ChannelId id, ChannelId remote_id) {
  FXL_DCHECK(initialized_);

  LinkData& link_data = FindLinkData(handle);
  auto cb_iter = link_data.outbound_conn_cbs.find(psm);
  FXL_DCHECK(cb_iter != link_data.outbound_conn_cbs.end())
      << "l2cap: no previous call to OpenChannel with PSM " << psm;

  std::list<ChannelDelivery>& handlers = cb_iter->second;
  FXL_DCHECK(!handlers.empty());

  ChannelCallback& cb = handlers.front().first;
  auto chan = OpenFakeChannel(&link_data, id, remote_id);
  async_dispatcher_t* const dispatcher = handlers.front().second;
  async::PostTask(dispatcher, [cb = std::move(cb), chan = std::move(chan)] {
    cb(std::move(chan));
  });

  handlers.pop_front();
  if (handlers.empty()) {
    link_data.outbound_conn_cbs.erase(cb_iter);
  }
}

void FakeLayer::TriggerInboundChannel(hci::ConnectionHandle handle, PSM psm,
                                      ChannelId id, ChannelId remote_id) {
  FXL_DCHECK(initialized_);

  LinkData& link_data = FindLinkData(handle);
  auto cb_iter = inbound_conn_cbs_.find(psm);
  FXL_DCHECK(cb_iter != inbound_conn_cbs_.end())
      << "l2cap: no service registered for PSM " << psm;

  ChannelCallback& cb = cb_iter->second.first;
  async_dispatcher_t* const dispatcher = cb_iter->second.second;
  auto chan = OpenFakeChannel(&link_data, id, remote_id);
  async::PostTask(dispatcher, [cb = std::move(cb), chan = std::move(chan)] {
    cb(std::move(chan));
  });
}

void FakeLayer::Initialize() { initialized_ = true; }

void FakeLayer::ShutDown() { initialized_ = false; }

void FakeLayer::AddACLConnection(hci::ConnectionHandle handle,
                                 hci::Connection::Role role,
                                 LinkErrorCallback link_error_cb,
                                 async_dispatcher_t* dispatcher) {
  if (!initialized_)
    return;

  RegisterInternal(handle, role, hci::Connection::LinkType::kACL,
                   std::move(link_error_cb), dispatcher);
}

void FakeLayer::AddLEConnection(
    hci::ConnectionHandle handle, hci::Connection::Role role,
    LEConnectionParameterUpdateCallback conn_param_cb,
    LinkErrorCallback link_error_cb, AddLEConnectionCallback channel_callback,
    async_dispatcher_t* dispatcher) {
  if (!initialized_)
    return;

  LinkData* data =
      RegisterInternal(handle, role, hci::Connection::LinkType::kLE,
                       std::move(link_error_cb), dispatcher);
  data->le_conn_param_cb = std::move(conn_param_cb);

  // Open the ATT and SMP fixed channels.
  auto att = OpenFakeFixedChannel(data, kATTChannelId);
  auto smp = OpenFakeFixedChannel(data, kLESMPChannelId);
  async::PostTask(dispatcher, [att = std::move(att), smp = std::move(smp),
                               cb = std::move(channel_callback)]() mutable {
    cb(std::move(att), std::move(smp));
  });
}

void FakeLayer::RemoveConnection(hci::ConnectionHandle handle) {
  links_.erase(handle);
}

void FakeLayer::OpenChannel(hci::ConnectionHandle handle, PSM psm,
                            ChannelCallback cb,
                            async_dispatcher_t* dispatcher) {
  if (!initialized_)
    return;

  LinkData& link_data = FindLinkData(handle);
  link_data.outbound_conn_cbs[psm].push_back(
      std::make_pair(std::move(cb), dispatcher));
}

bool FakeLayer::RegisterService(PSM psm, ChannelCallback cb,
                                async_dispatcher_t* dispatcher) {
  if (!initialized_)
    return false;

  auto result =
      inbound_conn_cbs_.emplace(psm, std::make_pair(std::move(cb), dispatcher));
  return result.second;
}

void FakeLayer::UnregisterService(PSM psm) {
  if (!initialized_)
    return;

  inbound_conn_cbs_.erase(psm);
}

FakeLayer::LinkData* FakeLayer::RegisterInternal(
    hci::ConnectionHandle handle, hci::Connection::Role role,
    hci::Connection::LinkType link_type, LinkErrorCallback link_error_cb,
    async_dispatcher_t* dispatcher) {
  FXL_DCHECK(links_.find(handle) == links_.end())
      << "l2cap: Connection handle re-used!";

  LinkData data;
  data.handle = handle;
  data.role = role;
  data.type = link_type;
  data.link_error_cb = std::move(link_error_cb);
  data.dispatcher = dispatcher;

  auto insert_res = links_.emplace(handle, std::move(data));
  return &insert_res.first->second;
}

fbl::RefPtr<FakeChannel> FakeLayer::OpenFakeChannel(LinkData* link,
                                                    ChannelId id,
                                                    ChannelId remote_id) {
  auto chan =
      fbl::AdoptRef(new FakeChannel(id, remote_id, link->handle, link->type));
  chan->SetLinkErrorCallback(link->link_error_cb.share(), link->dispatcher);

  if (chan_cb_) {
    chan_cb_(chan);
  }

  return chan;
}

fbl::RefPtr<FakeChannel> FakeLayer::OpenFakeFixedChannel(LinkData* link,
                                                         ChannelId id) {
  return OpenFakeChannel(link, id, id);
}

FakeLayer::LinkData& FakeLayer::FindLinkData(hci::ConnectionHandle handle) {
  auto link_iter = links_.find(handle);
  FXL_DCHECK(link_iter != links_.end())
      << "l2cap: fake link not found: (handle: " << handle << ")";
  return link_iter->second;
}

}  // namespace testing
}  // namespace l2cap
}  // namespace btlib
