// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETWORK_NETWORK_SERVICE_IMPL_H_
#define APPS_NETWORK_NETWORK_SERVICE_IMPL_H_

#include "apps/network/services/network_service.fidl.h"
#include "lib/netstack/fidl/net_address.fidl.h"

#include <list>
#include <memory>
#include <queue>

#include "apps/network/url_loader_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"
#include "mx/socket.h"

namespace network {

class NetworkServiceImpl : public NetworkService,
                           public URLLoaderImpl::Coordinator {
 public:
  NetworkServiceImpl();
  ~NetworkServiceImpl() override;

  void AddBinding(fidl::InterfaceRequest<NetworkService> request);

  // NetworkService methods:
  void CreateURLLoader(fidl::InterfaceRequest<URLLoader> request) override;
  void GetCookieStore(mx::channel cookie_store) override;
  void CreateWebSocket(mx::channel socket) override;
  void CreateTCPBoundSocket(
      netstack::NetAddressPtr local_address,
      mx::channel bound_socket,
      const CreateTCPBoundSocketCallback& callback) override;
  void CreateTCPConnectedSocket(
      netstack::NetAddressPtr remote_address,
      mx::socket send_stream,
      mx::socket receive_stream,
      mx::channel client_socket,
      const CreateTCPConnectedSocketCallback& callback) override;
  void CreateUDPSocket(mx::channel socket) override;
  void CreateHttpServer(netstack::NetAddressPtr local_address,
                        mx::channel delegate,
                        const CreateHttpServerCallback& callback) override;
  void RegisterURLLoaderInterceptor(mx::channel factory) override;
  void CreateHostResolver(mx::channel host_resolver) override;

 private:
  class UrlLoaderContainer;

  // URLLoaderImpl::Coordinator:
  void RequestNetworkSlot(
      std::function<void(ftl::Closure)> slot_request) override;

  void OnSlotReturned();

  size_t available_slots_;
  fidl::BindingSet<NetworkService> bindings_;
  std::list<UrlLoaderContainer> loaders_;
  std::queue<std::function<void(ftl::Closure)>> slot_requests_;
};

}  // namespace network

#endif  // APPS_NETWORK_NETWORK_SERVICE_IMPL_H_
