// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_service_impl.h"

#include "apps/network/mojo/services/network/net_adapters.h"
#include "apps/network/mojo/services/network/net_errors.h"
#include "apps/network/mojo/services/network/network_service_impl.h"
#include "apps/network/mojo/services/network/url_loader_impl.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace mojo {

NetworkServiceImpl::NetworkServiceImpl(InterfaceRequest<NetworkService> request)
    : binding_(this, request.Pass()) {
}

NetworkServiceImpl::~NetworkServiceImpl() {
}

void NetworkServiceImpl::CreateURLLoader(InterfaceRequest<URLLoader> loader) {
}

void NetworkServiceImpl::GetCookieStore(ScopedMessagePipeHandle cookie_store) {
  FTL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateWebSocket(ScopedMessagePipeHandle socket) {
  FTL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateTCPBoundSocket(
    NetAddressPtr local_address,
    ScopedMessagePipeHandle bound_socket,
    const CreateTCPBoundSocketCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback.Run(MakeNetworkError(net::ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::CreateTCPConnectedSocket(
    NetAddressPtr remote_address,
    ScopedDataPipeConsumerHandle send_stream,
    ScopedDataPipeProducerHandle receive_stream,
    ScopedMessagePipeHandle client_socket,
    const CreateTCPConnectedSocketCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback.Run(MakeNetworkError(net::ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::CreateUDPSocket(ScopedMessagePipeHandle request) {
  FTL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateHttpServer(
    NetAddressPtr local_address,
    ScopedMessagePipeHandle delegate,
    const CreateHttpServerCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback.Run(MakeNetworkError(net::ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::RegisterURLLoaderInterceptor(
    ScopedMessagePipeHandle factory) {
  FTL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateHostResolver(
    ScopedMessagePipeHandle host_resolver) {
  FTL_NOTIMPLEMENTED();
}

}  // namespace mojo
