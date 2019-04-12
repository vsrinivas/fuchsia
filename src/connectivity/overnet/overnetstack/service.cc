// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/overnetstack/service.h"

#include "src/connectivity/overnet/lib/protocol/fidl.h"

namespace overnetstack {

Service::Service(OvernetApp* app) : app_(app) {}

overnet::Status Service::Start() {
  app_->component_context()->outgoing()->AddPublicService(
      bindings_.GetHandler(this));
  return overnet::Status::Ok();
}

void Service::ListPeers(uint64_t last_seen_version,
                        ListPeersCallback callback) {
  using Peer = fuchsia::overnet::Peer;
  app_->endpoint()->OnNodeDescriptionTableChange(
      last_seen_version,
      overnet::Callback<void>(
          overnet::ALLOCATED_CALLBACK, [this, callback = std::move(callback)] {
            std::vector<Peer> response;
            auto new_version = app_->endpoint()->ForEachNodeDescription(
                [&response, self_node = app_->endpoint()->node_id()](
                    overnet::NodeId id,
                    const fuchsia::overnet::protocol::PeerDescription& m) {
                  Peer peer;
                  peer.id = id.as_fidl();
                  peer.is_self = id == self_node;
                  peer.description = fidl::Clone(m);
                  response.emplace_back(std::move(peer));
                });
            callback(new_version, std::move(response));
          }));
}

void Service::RegisterService(
    std::string service_name,
    fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider> provider) {
  class ServiceProvider final : public OvernetApp::ServiceProvider {
   public:
    explicit ServiceProvider(OvernetApp* app, std::string fully_qualified_name,
                             fuchsia::overnet::protocol::ReliabilityAndOrdering
                                 reliability_and_ordering,
                             fuchsia::overnet::ServiceProviderPtr provider)
        : OvernetApp::ServiceProvider(app, fully_qualified_name,
                                      reliability_and_ordering),
          provider_(std::move(provider)) {
      provider_.set_error_handler([this](zx_status_t) { Close(); });
    }
    void Connect(zx::channel channel) override final {
      provider_->ConnectToService(std::move(channel));
    }

   private:
    fuchsia::overnet::ServiceProviderPtr provider_;
  };
  app_->InstantiateServiceProvider<ServiceProvider>(
      app_, std::move(service_name),
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      provider.Bind());
}

void Service::ConnectToService(fuchsia::overnet::protocol::NodeId node,
                               std::string service_name, zx::channel channel) {
  auto node_id = overnet::NodeId(node);
  if (app_->endpoint()->node_id() == node_id) {
    app_->ConnectToLocalService(std::move(service_name), std::move(channel));
  } else {
    auto ns = app_->endpoint()->InitiateStream(
        node_id,
        fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
        std::move(service_name));
    if (ns.is_error()) {
      OVERNET_TRACE(ERROR) << "ConnectToService failed: " << ns.AsStatus();
    } else {
      app_->BindChannel(std::move(*ns), std::move(channel));
    }
  }
}

}  // namespace overnetstack
