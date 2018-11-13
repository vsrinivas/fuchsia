// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "service.h"
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
  app_->endpoint()->ForEachNodeMetric([&response,
                                       self_node = app_->endpoint()->node_id()](
                                          const overnet::NodeMetrics& m) {
    std::vector<uint8_t> desc(m.description().begin(), m.description().end());
    auto desc_status = overnet::Decode<fuchsia::overnet::PeerDescription>(
        desc.data(), desc.size());
    if (desc_status.is_error()) {
      OVERNET_TRACE(WARNING) << "Omit peer with badly encoded description: "
                             << desc_status.AsStatus();
      return;
    }
    response.emplace_back(Peer{m.node_id().get(), m.node_id() == self_node,
                               std::move(*desc_status)});
  });
  callback(fidl::VectorPtr<Peer>(std::move(response)));
}

void Service::RegisterService(
    fidl::StringPtr service_name,
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
      service_name.get(), std::make_unique<ServiceProvider>(provider.Bind()));
}

void Service::ConnectToService(uint64_t node, fidl::StringPtr service_name,
                               zx::channel channel) {
  auto node_id = overnet::NodeId(node);
  fuchsia::overnet::protocol::Introduction intro;
  intro.set_service_name(service_name.get());
  if (app_->endpoint()->node_id() == node_id) {
    app_->ConnectToLocalService(intro, std::move(channel));
  } else {
    app_->endpoint()->SendIntro(
        node_id,
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