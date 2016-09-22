// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_FAKE_NETWORK_SERVICE_FAKE_NETWORK_SERVICE_H_
#define APPS_LEDGER_FAKE_NETWORK_SERVICE_FAKE_NETWORK_SERVICE_H_

#include <memory>
#include <vector>

#include "apps/ledger/fake_network_service/fake_url_loader.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "apps/network/interfaces/network_service.mojom.h"

namespace fake_network_service {

// Fake implementation of network service, allowing to inspect the last request
// passed to any url loader and set the response that url loaders need to
// return. Response is moved out when url request starts, and needs to be set
// each time.
class FakeNetworkService : public mojo::NetworkService {
 public:
  FakeNetworkService(mojo::InterfaceRequest<NetworkService> request);
  ~FakeNetworkService() override;

  mojo::URLRequest* GetRequest() { return request_received_.get(); }

  void SetResponse(mojo::URLResponsePtr response) {
    response_to_return_ = std::move(response);
  }

  // NetworkService:
  void CreateURLLoader(mojo::InterfaceRequest<mojo::URLLoader> loader) override;
  void GetCookieStore(mojo::ScopedMessagePipeHandle cookie_store) override;
  void CreateWebSocket(mojo::ScopedMessagePipeHandle socket) override;
  void CreateTCPBoundSocket(
      mojo::NetAddressPtr local_address,
      mojo::ScopedMessagePipeHandle bound_socket,
      const CreateTCPBoundSocketCallback& callback) override;
  void CreateTCPConnectedSocket(
      mojo::NetAddressPtr remote_address,
      mojo::ScopedDataPipeConsumerHandle send_stream,
      mojo::ScopedDataPipeProducerHandle receive_stream,
      mojo::ScopedMessagePipeHandle client_socket,
      const CreateTCPConnectedSocketCallback& callback) override;
  void CreateUDPSocket(mojo::ScopedMessagePipeHandle socket) override;
  void CreateHttpServer(mojo::NetAddressPtr local_address,
                        mojo::ScopedMessagePipeHandle delegate,
                        const CreateHttpServerCallback& callback) override;
  void RegisterURLLoaderInterceptor(
      mojo::ScopedMessagePipeHandle factory) override;
  void CreateHostResolver(mojo::ScopedMessagePipeHandle host_resolver) override;

 private:
  mojo::Binding<NetworkService> binding_;
  std::vector<std::unique_ptr<FakeURLLoader>> loaders_;
  mojo::URLRequestPtr request_received_;
  mojo::URLResponsePtr response_to_return_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FakeNetworkService);
};

}  // namespace fake_network_service

#endif  // APPS_LEDGER_FAKE_NETWORK_SERVICE_FAKE_NETWORK_SERVICE_H_
