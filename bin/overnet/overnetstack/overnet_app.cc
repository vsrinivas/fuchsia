// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "overnet_app.h"
#include <fuchsia/overnet/cpp/fidl.h>
#include "bound_channel.h"
#include "fuchsia_port.h"
#include "garnet/lib/overnet/protocol/fidl.h"

namespace overnetstack {

OvernetApp::OvernetApp(overnet::Timer* timer)
    : startup_context_(component::StartupContext::CreateFromStartupInfo()),
      timer_(timer) {
  UpdateDescription();
}

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
  ReadNextIntroduction();
  return overnet::Status::Ok();
}

void OvernetApp::RegisterServiceProvider(
    const std::string& name, std::unique_ptr<ServiceProvider> provider) {
  service_providers_.emplace(name, std::move(provider));
  UpdateDescription();
}

void OvernetApp::UpdateDescription() {
  fuchsia::overnet::PeerDescription desc;
  for (const auto& svc : service_providers_) {
    desc.services.push_back(svc.first);
  }
  endpoint_.SetDescription(*overnet::Encode(&desc));
}

void OvernetApp::BindStream(overnet::RouterEndpoint::NewStream ns,
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

void OvernetApp::ConnectToLocalService(
    const fuchsia::overnet::protocol::Introduction& intro,
    zx::channel channel) {
  if (!intro.has_service_name()) {
    OVERNET_TRACE(DEBUG) << "No service name in local service request";
    return;
  }
  auto it = service_providers_.find(*intro.service_name());
  if (it == service_providers_.end()) {
    OVERNET_TRACE(DEBUG) << "Local service not found: "
                         << *intro.service_name();
    return;
  }
  it->second->Connect(intro, std::move(channel));
}

void OvernetApp::ReadNextIntroduction() {
  // Loop, reading service creation requests, and attempting to bind them to
  // local services.
  endpoint_.RecvIntro(
      [this](overnet::StatusOr<overnet::RouterEndpoint::ReceivedIntroduction>
                 status) {
        if (status.is_error()) {
          OVERNET_TRACE(ERROR)
              << "Failed to read introduction: " << status.AsStatus();
          return;
        }
        zx_handle_t a, b;
        auto err = zx_channel_create(0, &a, &b);
        if (err != ZX_OK) {
          status->new_stream.Fail(
              overnet::Status::FromZx(err).WithContext("ReadNextIntroduction"));
          return;
        }
        BindStream(std::move(status->new_stream), zx::channel(a));
        ConnectToLocalService(std::move(status->introduction), zx::channel(b));
        ReadNextIntroduction();
      });
}

}  // namespace overnetstack
