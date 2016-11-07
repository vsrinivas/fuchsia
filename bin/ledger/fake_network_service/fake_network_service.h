// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_FAKE_NETWORK_SERVICE_FAKE_NETWORK_SERVICE_H_
#define APPS_LEDGER_SRC_FAKE_NETWORK_SERVICE_FAKE_NETWORK_SERVICE_H_

#include <memory>
#include <vector>

#include "apps/ledger/src/fake_network_service/fake_url_loader.h"
#include "apps/network/services/network_service.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"

namespace fake_network_service {

// Fake implementation of network service, allowing to inspect the last request
// passed to any url loader and set the response that url loaders need to
// return. Response is moved out when url request starts, and needs to be set
// each time.
class FakeNetworkService : public network::NetworkService {
 public:
  FakeNetworkService(fidl::InterfaceRequest<NetworkService> request);
  ~FakeNetworkService() override;

  network::URLRequest* GetRequest() { return request_received_.get(); }

  void SetResponse(network::URLResponsePtr response) {
    response_to_return_ = std::move(response);
  }

  // NetworkService:
  void CreateURLLoader(
      fidl::InterfaceRequest<network::URLLoader> loader) override;
  void GetCookieStore(mx::channel cookie_store) override;
  void CreateWebSocket(mx::channel socket) override;
  void CreateTCPBoundSocket(
      network::NetAddressPtr local_address,
      mx::channel bound_socket,
      const CreateTCPBoundSocketCallback& callback) override;
  void CreateTCPConnectedSocket(
      network::NetAddressPtr remote_address,
      mx::datapipe_consumer send_stream,
      mx::datapipe_producer receive_stream,
      mx::channel client_socket,
      const CreateTCPConnectedSocketCallback& callback) override;
  void CreateUDPSocket(mx::channel socket) override;
  void CreateHttpServer(network::NetAddressPtr local_address,
                        mx::channel delegate,
                        const CreateHttpServerCallback& callback) override;
  void RegisterURLLoaderInterceptor(mx::channel factory) override;
  void CreateHostResolver(mx::channel host_resolver) override;

 private:
  fidl::Binding<NetworkService> binding_;
  std::vector<std::unique_ptr<FakeURLLoader>> loaders_;
  network::URLRequestPtr request_received_;
  network::URLResponsePtr response_to_return_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FakeNetworkService);
};

}  // namespace fake_network_service

#endif  // APPS_LEDGER_SRC_FAKE_NETWORK_SERVICE_FAKE_NETWORK_SERVICE_H_
