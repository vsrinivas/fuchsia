// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/overnet/overnetstack/service.h"
#include "garnet/lib/overnet/protocol/fidl.h"

namespace overnetstack {

Service::Service(OvernetApp* app) : app_(app) {}

overnet::Status Service::Start() {
  app_->startup_context()->outgoing().AddPublicService(
      bindings_.GetHandler(this));
  return overnet::Status::Ok();
}

void Service::ListPeers(ListPeersCallback callback) {
  using Peer = fuchsia::overnet::Peer;
  std::vector<Peer> response;
  app_->endpoint()->ForEachNodeMetric(
      [&response, self_node = app_->endpoint()->node_id()](
          const fuchsia::overnet::protocol::NodeMetrics& m) {
        Peer peer;
        peer.id = m.label()->id;
        peer.is_self = m.label()->id == self_node;
        if (m.has_description()) {
          peer.description = fidl::Clone(*m.description());
        }
        response.emplace_back(std::move(peer));
      });
  callback(std::move(response));
}

void Service::RegisterService(
    std::string service_name,
    fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider> provider) {
  class ServiceProvider final : public OvernetApp::ServiceProvider {
   public:
    explicit ServiceProvider(fuchsia::overnet::ServiceProviderPtr provider)
        : provider_(std::move(provider)) {}
    void Connect(const fuchsia::overnet::protocol::Introduction& intro,
                 zx::channel channel) final {
      if (!intro.has_service_name()) {
        OVERNET_TRACE(DEBUG) << "No service name in local service request";
        return;
      }
      provider_->ConnectToService(*intro.service_name(), std::move(channel));
    }

   private:
    const fuchsia::overnet::ServiceProviderPtr provider_;
  };
  app_->RegisterServiceProvider(
      service_name, std::make_unique<ServiceProvider>(provider.Bind()));
}

void Service::ConnectToService(fuchsia::overnet::protocol::NodeId node,
                               std::string service_name, zx::channel channel) {
  fuchsia::overnet::protocol::Introduction intro;
  intro.set_service_name(service_name);
  if (app_->endpoint()->node_id() == node) {
    app_->ConnectToLocalService(intro, std::move(channel));
  } else {
    app_->endpoint()->SendIntro(
        node,
        fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
        std::move(intro),
        overnet::StatusOrCallback<overnet::RouterEndpoint::NewStream>(
            overnet::ALLOCATED_CALLBACK,
            [this, channel = std::move(channel)](
                overnet::StatusOr<overnet::RouterEndpoint::NewStream>
                    ns) mutable {
              if (ns.is_error()) {
                OVERNET_TRACE(ERROR)
                    << "ConnectToService failed: " << ns.AsStatus();
              } else {
                app_->BindStream(std::move(*ns), std::move(channel));
              }
            }));
  }
}

}  // namespace overnetstack
