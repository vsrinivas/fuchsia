// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"

#include "mojo/services/network/network_service_impl.h"
#include "mojo/services/network/network_error.h"
#include "mojo/services/network/net_adapters.h"

#include "mojo/services/network/url_loader_impl.h"
#include "mojo/public/cpp/application/application_connection.h"

namespace mojo {

NetworkServiceImpl::NetworkServiceImpl(InterfaceRequest<NetworkService> request,
                                       ApplicationConnection* connection)
    : binding_(this, request.Pass()) {
}

NetworkServiceImpl::~NetworkServiceImpl() {
}

void NetworkServiceImpl::CreateURLLoader(InterfaceRequest<URLLoader> loader) {
  new URLLoaderImpl(loader.Pass());
}

void NetworkServiceImpl::GetCookieStore(ScopedMessagePipeHandle cookie_store) {
  NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateWebSocket(ScopedMessagePipeHandle socket) {
  NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateTCPBoundSocket(
    NetAddressPtr local_address,
    ScopedMessagePipeHandle bound_socket,
    const CreateTCPBoundSocketCallback& callback) {
  NOTIMPLEMENTED();
  callback.Run(MakeNetworkError(ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::CreateTCPConnectedSocket(
    NetAddressPtr remote_address,
    ScopedDataPipeConsumerHandle send_stream,
    ScopedDataPipeProducerHandle receive_stream,
    ScopedMessagePipeHandle client_socket,
    const CreateTCPConnectedSocketCallback& callback) {
  NOTIMPLEMENTED();
  callback.Run(MakeNetworkError(ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::CreateUDPSocket(ScopedMessagePipeHandle request) {
  NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateHttpServer(
    NetAddressPtr local_address,
    ScopedMessagePipeHandle delegate,
    const CreateHttpServerCallback& callback) {
  NOTIMPLEMENTED();
  callback.Run(MakeNetworkError(ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::RegisterURLLoaderInterceptor(
    ScopedMessagePipeHandle factory) {
  NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateHostResolver(
    ScopedMessagePipeHandle host_resolver) {
  NOTIMPLEMENTED();
}

}  // namespace mojo
