// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_service_impl.h"

#include <utility>

#include "apps/network/net_adapters.h"
#include "apps/network/net_errors.h"
#include "apps/network/network_service_impl.h"
#include "apps/network/url_loader_impl.h"
#include "lib/ftl/logging.h"

namespace network {

NetworkServiceImpl::NetworkServiceImpl() = default;

NetworkServiceImpl::~NetworkServiceImpl() = default;

void NetworkServiceImpl::AddBinding(
    fidl::InterfaceRequest<NetworkService> request) {
  bindings_.AddBinding(this, std::move(request));
}

void NetworkServiceImpl::CreateURLLoader(
    fidl::InterfaceRequest<URLLoader> loader) {
  auto impl = std::make_unique<URLLoaderImpl>(std::move(loader));
  url_loader_bindings_.AddBinding(std::move(impl));
}

void NetworkServiceImpl::GetCookieStore(mx::channel cookie_store) {
  FTL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateWebSocket(mx::channel socket) {
  FTL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateTCPBoundSocket(
    NetAddressPtr local_address,
    mx::channel bound_socket,
    const CreateTCPBoundSocketCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(MakeNetworkError(network::NETWORK_ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::CreateTCPConnectedSocket(
    NetAddressPtr remote_address,
    mx::datapipe_consumer send_stream,
    mx::datapipe_producer receive_stream,
    mx::channel client_socket,
    const CreateTCPConnectedSocketCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(MakeNetworkError(network::NETWORK_ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::CreateUDPSocket(mx::channel request) {
  FTL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateHttpServer(
    NetAddressPtr local_address,
    mx::channel delegate,
    const CreateHttpServerCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(MakeNetworkError(network::NETWORK_ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::RegisterURLLoaderInterceptor(mx::channel factory) {
  FTL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateHostResolver(mx::channel host_resolver) {
  FTL_NOTIMPLEMENTED();
}

}  // namespace network
