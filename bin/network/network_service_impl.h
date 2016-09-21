// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_NETWORK_NETWORK_SERVICE_IMPL_H_
#define MOJO_SERVICES_NETWORK_NETWORK_SERVICE_IMPL_H_

#include "apps/network/interfaces/network_service.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace mojo {
class ApplicationConnection;

class NetworkServiceImpl : public NetworkService {
 public:
  NetworkServiceImpl(InterfaceRequest<NetworkService> request);
  ~NetworkServiceImpl() override;

  // NetworkService methods:
  void CreateURLLoader(InterfaceRequest<URLLoader> loader) override;
  void GetCookieStore(ScopedMessagePipeHandle cookie_store) override;
  void CreateWebSocket(ScopedMessagePipeHandle socket) override;
  void CreateTCPBoundSocket(
      NetAddressPtr local_address,
      ScopedMessagePipeHandle bound_socket,
      const CreateTCPBoundSocketCallback& callback) override;
  void CreateTCPConnectedSocket(
      NetAddressPtr remote_address,
      ScopedDataPipeConsumerHandle send_stream,
      ScopedDataPipeProducerHandle receive_stream,
      ScopedMessagePipeHandle client_socket,
      const CreateTCPConnectedSocketCallback& callback) override;
  void CreateUDPSocket(ScopedMessagePipeHandle socket) override;
  void CreateHttpServer(NetAddressPtr local_address,
                        ScopedMessagePipeHandle delegate,
                        const CreateHttpServerCallback& callback) override;
  void RegisterURLLoaderInterceptor(
      ScopedMessagePipeHandle factory) override;
  void CreateHostResolver(ScopedMessagePipeHandle host_resolver) override;

 private:
  StrongBinding<NetworkService> binding_;
};

}  // namespace mojo

#endif  // MOJO_SERVICES_NETWORK_NETWORK_SERVICE_IMPL_H_
