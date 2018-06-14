// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_layer.h"

#include <lib/async/cpp/task.h>

#include "fake_channel.h"

namespace btlib {
namespace l2cap {
namespace testing {

void FakeLayer::Initialize() {
  initialized_ = true;
}

void FakeLayer::ShutDown() {
  initialized_ = false;
}

void FakeLayer::TriggerLEConnectionParameterUpdate(
    hci::ConnectionHandle handle,
    const hci::LEPreferredConnectionParameters& params) {
  FXL_DCHECK(initialized_);

  auto iter = links_.find(handle);
  FXL_DCHECK(iter != links_.end())
      << "l2cap: fake link not found: (handle: " << handle << ")";

  LinkData& link_data = iter->second;
  async::PostTask(link_data.dispatcher,
                  [params, cb = link_data.le_conn_param_cb.share()] { cb(params); });
}

void FakeLayer::AddACLConnection(hci::ConnectionHandle handle,
                                 hci::Connection::Role role,
                                 LinkErrorCallback link_error_cb,
                                 async_t* dispatcher) {
  if (!initialized_)
    return;

  RegisterInternal(handle, role, hci::Connection::LinkType::kACL,
                   std::move(link_error_cb), dispatcher);
}

void FakeLayer::AddLEConnection(
    hci::ConnectionHandle handle, hci::Connection::Role role,
    LEConnectionParameterUpdateCallback conn_param_cb,
    LinkErrorCallback link_error_cb, AddLEConnectionCallback channel_callback,
    async_t* dispatcher) {
  if (!initialized_)
    return;

  LinkData* data = RegisterInternal(handle, role,
                                    hci::Connection::LinkType::kLE,
                                    std::move(link_error_cb), dispatcher);
  data->le_conn_param_cb = std::move(conn_param_cb);

  // Open the ATT and SMP fixed channels.
  auto att = OpenFakeChannel(data, kATTChannelId);
  auto smp = OpenFakeChannel(data, kLESMPChannelId);
  async::PostTask(dispatcher, [att = std::move(att), smp = std::move(smp),
                               cb = std::move(channel_callback)]() mutable {
    cb(std::move(att), std::move(smp));
  });
}

void FakeLayer::RemoveConnection(hci::ConnectionHandle handle) {
  links_.erase(handle);
}

fbl::RefPtr<Channel> FakeLayer::OpenFakeChannel(LinkData* link, ChannelId id) {
  auto chan = fbl::AdoptRef(new FakeChannel(id, link->handle, link->type));
  chan->SetLinkErrorCallback(link->link_error_cb.share(), link->dispatcher);

  if (chan_cb_) {
    chan_cb_(chan);
  }

  return chan;
}

FakeLayer::LinkData* FakeLayer::RegisterInternal(
    hci::ConnectionHandle handle,
    hci::Connection::Role role,
    hci::Connection::LinkType link_type,
    LinkErrorCallback link_error_cb,
    async_t* dispatcher) {
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

}  // namespace testing
}  // namespace l2cap
}  // namespace btlib
