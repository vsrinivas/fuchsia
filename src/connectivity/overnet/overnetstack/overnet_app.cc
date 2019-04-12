// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/overnetstack/overnet_app.h"

#include <fuchsia/overnet/cpp/fidl.h>

#include "src/connectivity/overnet/lib/protocol/fidl.h"
#include "src/connectivity/overnet/overnetstack/bound_channel.h"
#include "src/connectivity/overnet/overnetstack/bound_socket.h"

namespace overnetstack {

OvernetApp::OvernetApp(overnet::Timer* timer)
    : component_context_(sys::ComponentContext::Create()), timer_(timer) {}

OvernetApp::~OvernetApp() {}

overnet::NodeId OvernetApp::GenerateNodeId() {
  uint64_t out;
  zx_cprng_draw(&out, sizeof(out));
  return overnet::NodeId(out);
}

overnet::Status OvernetApp::Start() {
  for (size_t i = 0; i < actors_.size(); i++) {
    auto status = actors_[i]->Start();
    if (status.is_error()) {
      actors_.resize(i);
      return status.WithContext("Trying to start actor");
    }
  }
  return overnet::Status::Ok();
}

void OvernetApp::BindChannel(overnet::RouterEndpoint::NewStream ns,
                             zx::channel channel) {
  ZX_ASSERT(channel.is_valid());
  zx_info_handle_basic_t info;
  auto err = channel.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                              nullptr, nullptr);
  ZX_ASSERT(err == ZX_OK);
  ZX_ASSERT(info.type == ZX_OBJ_TYPE_CHANNEL);
  new BoundChannel(this, std::move(ns), std::move(channel));
  ZX_ASSERT(!channel.is_valid());
}

void OvernetApp::BindSocket(overnet::RouterEndpoint::NewStream ns,
                            zx::socket socket) {
  ZX_ASSERT(socket.is_valid());
  zx_info_handle_basic_t info;
  auto err = socket.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr,
                             nullptr);
  ZX_ASSERT(err == ZX_OK);
  ZX_ASSERT(info.type == ZX_OBJ_TYPE_SOCKET);
  new BoundSocket(this, std::move(ns), std::move(socket));
  ZX_ASSERT(!socket.is_valid());
}

void OvernetApp::ConnectToLocalService(const std::string& service_name,
                                       zx::channel channel) {
  auto it = service_providers_.find(service_name);
  if (it == service_providers_.end()) {
    OVERNET_TRACE(DEBUG) << "Local service not found: " << service_name;
    return;
  }
  it->second->Connect(std::move(channel));
}

void OvernetApp::ServiceProvider::AcceptStream(
    overnet::RouterEndpoint::NewStream stream) {
  zx_handle_t a, b;
  auto err = zx_channel_create(0, &a, &b);
  if (err != ZX_OK) {
    stream.Fail(overnet::Status::FromZx(err).WithContext("AcceptStream"));
    return;
  }
  app_->BindChannel(std::move(stream), zx::channel(a));
  Connect(zx::channel(b));
}

}  // namespace overnetstack
