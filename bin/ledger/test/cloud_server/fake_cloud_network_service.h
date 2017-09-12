// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TEST_CLOUD_SERVER_FAKE_CLOUD_NETWORK_SERVICE_H_
#define APPS_LEDGER_SRC_TEST_CLOUD_SERVER_FAKE_CLOUD_NETWORK_SERVICE_H_

#include "apps/ledger/src/test/cloud_server/fake_cloud_url_loader.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/netstack/fidl/net_address.fidl.h"
#include "lib/network/fidl/network_service.fidl.h"

namespace ledger {

// Implementation of network::NetworkService that simulates Firebase and GCS
// servers.
class FakeCloudNetworkService : public network::NetworkService {
 public:
  FakeCloudNetworkService();
  ~FakeCloudNetworkService() override;

  // network::NetworkService
  void CreateURLLoader(
      ::fidl::InterfaceRequest<network::URLLoader> loader) override;
  void GetCookieStore(mx::channel cookie_store) override;
  void CreateWebSocket(mx::channel socket) override;
  using CreateTCPBoundSocketCallback =
      std::function<void(network::NetworkErrorPtr, netstack::NetAddressPtr)>;
  void CreateTCPBoundSocket(
      netstack::NetAddressPtr local_address,
      mx::channel bound_socket,
      const CreateTCPBoundSocketCallback& callback) override;
  using CreateTCPConnectedSocketCallback =
      std::function<void(network::NetworkErrorPtr, netstack::NetAddressPtr)>;
  void CreateTCPConnectedSocket(
      netstack::NetAddressPtr remote_address,
      mx::socket send_stream,
      mx::socket receive_stream,
      mx::channel client_socket,
      const CreateTCPConnectedSocketCallback& callback) override;
  void CreateUDPSocket(mx::channel socket) override;
  using CreateHttpServerCallback =
      std::function<void(network::NetworkErrorPtr, netstack::NetAddressPtr)>;
  void CreateHttpServer(netstack::NetAddressPtr local_address,
                        mx::channel delegate,
                        const CreateHttpServerCallback& callback) override;
  void RegisterURLLoaderInterceptor(mx::channel factory) override;
  void CreateHostResolver(mx::channel host_resolver) override;

  // Bind a new request to this implementation.
  void AddBinding(fidl::InterfaceRequest<network::NetworkService> request);

 private:
  FakeCloudURLLoader url_loader_;
  fidl::BindingSet<network::URLLoader> loader_bindings_;
  fidl::BindingSet<network::NetworkService> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeCloudNetworkService);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_TEST_CLOUD_SERVER_FAKE_CLOUD_NETWORK_SERVICE_H_
