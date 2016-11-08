// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETWORK_NETWORK_SERVICE_IMPL_H_
#define APPS_NETWORK_NETWORK_SERVICE_IMPL_H_

#include "apps/network/services/network_service.fidl.h"

#include <memory>

#include "apps/network/url_loader_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"
#include "mx/datapipe.h"

namespace network {

class NetworkServiceImpl : public NetworkService {
 public:
  NetworkServiceImpl();
  ~NetworkServiceImpl() override;

  void AddBinding(fidl::InterfaceRequest<NetworkService> request);

  // NetworkService methods:
  void CreateURLLoader(fidl::InterfaceRequest<URLLoader> loader) override;
  void GetCookieStore(mx::channel cookie_store) override;
  void CreateWebSocket(mx::channel socket) override;
  void CreateTCPBoundSocket(
      NetAddressPtr local_address,
      mx::channel bound_socket,
      const CreateTCPBoundSocketCallback& callback) override;
  void CreateTCPConnectedSocket(
      NetAddressPtr remote_address,
      mx::datapipe_consumer send_stream,
      mx::datapipe_producer receive_stream,
      mx::channel client_socket,
      const CreateTCPConnectedSocketCallback& callback) override;
  void CreateUDPSocket(mx::channel socket) override;
  void CreateHttpServer(NetAddressPtr local_address,
                        mx::channel delegate,
                        const CreateHttpServerCallback& callback) override;
  void RegisterURLLoaderInterceptor(mx::channel factory) override;
  void CreateHostResolver(mx::channel host_resolver) override;

 private:
  fidl::BindingSet<NetworkService> bindings_;
  fidl::BindingSet<URLLoader, std::unique_ptr<URLLoaderImpl>> url_loader_bindings_;
};

}  // namespace network

#endif  // APPS_NETWORK_NETWORK_SERVICE_IMPL_H_
