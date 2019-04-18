// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/overnet/cpp/overnet_embedded.h>
#include "src/connectivity/overnet/lib/embedded/host_reactor.h"
#include "src/connectivity/overnet/lib/endpoint/router_endpoint.h"
#include "src/connectivity/overnet/lib/environment/trace_cout.h"

namespace overnet {

class OvernetEmbedded final : public fuchsia::overnet::embedded::Overnet {
 public:
  class Actor {
   public:
    Actor(OvernetEmbedded* root);
    virtual ~Actor();
    virtual Status Start() = 0;

   protected:
    OvernetEmbedded* root() const { return root_; }

   private:
    OvernetEmbedded* const root_;
  };

  OvernetEmbedded();
  ~OvernetEmbedded();

  void ListPeers(uint64_t last_seen_version,
                 ListPeersCallback callback) override;
  void RegisterService(
      std::string service_name,
      std::unique_ptr<fuchsia::overnet::embedded::ServiceProvider_Proxy>
          service_provider) override;
  void ConnectToService(fuchsia::overnet::protocol::embedded::NodeId node,
                        std::string service_name,
                        ClosedPtr<ZxChannel> channel) override;

  int Run();

  void Exit(const Status& status) { reactor_.Exit(status); }

  Timer* timer() { return &reactor_; }
  NodeId node_id() { return endpoint_.node_id(); }
  RouterEndpoint* endpoint() { return &endpoint_; }
  HostReactor* reactor() { return &reactor_; }

 private:
  static NodeId GenerateNodeId();
  void MaybeShutdown();

  TraceCout initial_log_{nullptr};
  ScopedRenderer initial_renderer_{&initial_log_};
  HostReactor reactor_;
  TraceCout running_log_{&reactor_};
  ScopedRenderer running_renderer_{&running_log_};
  RouterEndpoint endpoint_{&reactor_, GenerateNodeId(), true};
  std::map<std::string,
           std::unique_ptr<fuchsia::overnet::embedded::ServiceProvider_Proxy>>
      services_;
  std::unique_ptr<std::vector<Actor*>> actors_ =
      std::make_unique<std::vector<Actor*>>();
  Callback<void> shutdown_;
};

template <class Service>
std::unique_ptr<typename Service::Proxy_> ConnectToService(
    fuchsia::overnet::embedded::Overnet* overnet,
    const fuchsia::overnet::protocol::embedded::NodeId& peer) {
  auto [client, server] = ZxChannel::MakePair();
  overnet->ConnectToService(fidl::Clone(peer), Service::Name_,
                            std::move(server));
  return std::make_unique<typename Service::Proxy_>(std::move(client));
}

}  // namespace overnet
