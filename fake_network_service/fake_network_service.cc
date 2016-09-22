// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/fake_network_service/fake_network_service.h"

#include <utility>

#include "apps/ledger/fake_network_service/fake_url_loader.h"
#include "lib/ftl/logging.h"

namespace fake_network_service {

FakeNetworkService::FakeNetworkService(
    mojo::InterfaceRequest<NetworkService> request)
    : binding_(this, std::move(request)) {}

FakeNetworkService::~FakeNetworkService() {}

void FakeNetworkService::CreateURLLoader(
    mojo::InterfaceRequest<mojo::URLLoader> loader) {
  FTL_DCHECK(response_to_return_);
  loaders_.push_back(std::unique_ptr<FakeURLLoader>(new FakeURLLoader(
      std::move(loader), std::move(response_to_return_), &request_received_)));
}

void FakeNetworkService::GetCookieStore(
    mojo::ScopedMessagePipeHandle cookie_store) {
  FTL_DCHECK(false);
}

void FakeNetworkService::CreateWebSocket(mojo::ScopedMessagePipeHandle socket) {
  FTL_DCHECK(false);
}

void FakeNetworkService::CreateTCPBoundSocket(
    mojo::NetAddressPtr local_address,
    mojo::ScopedMessagePipeHandle bound_socket,
    const CreateTCPBoundSocketCallback& callback) {
  FTL_DCHECK(false);
}

void FakeNetworkService::CreateTCPConnectedSocket(
    mojo::NetAddressPtr remote_address,
    mojo::ScopedDataPipeConsumerHandle send_stream,
    mojo::ScopedDataPipeProducerHandle receive_stream,
    mojo::ScopedMessagePipeHandle client_socket,
    const CreateTCPConnectedSocketCallback& callback) {
  FTL_DCHECK(false);
}

void FakeNetworkService::CreateUDPSocket(mojo::ScopedMessagePipeHandle socket) {
  FTL_DCHECK(false);
}

void FakeNetworkService::CreateHttpServer(
    mojo::NetAddressPtr local_address,
    mojo::ScopedMessagePipeHandle delegate,
    const CreateHttpServerCallback& callback) {
  FTL_DCHECK(false);
}

void FakeNetworkService::RegisterURLLoaderInterceptor(
    mojo::ScopedMessagePipeHandle factory) {
  FTL_DCHECK(false);
}

void FakeNetworkService::CreateHostResolver(
    mojo::ScopedMessagePipeHandle host_resolver) {
  FTL_DCHECK(false);
}

}  // namespace fake_network_service
