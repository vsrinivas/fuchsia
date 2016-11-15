// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/fake_network_service/fake_network_service.h"

#include <utility>

#include "apps/ledger/src/fake_network_service/fake_url_loader.h"
#include "lib/ftl/logging.h"

namespace fake_network_service {

FakeNetworkService::FakeNetworkService(
    fidl::InterfaceRequest<NetworkService> request)
    : binding_(this, std::move(request)) {}

FakeNetworkService::~FakeNetworkService() {}

void FakeNetworkService::CreateURLLoader(
    fidl::InterfaceRequest<network::URLLoader> loader) {
  FTL_DCHECK(response_to_return_);
  loaders_.push_back(std::make_unique<FakeURLLoader>(
      std::move(loader), std::move(response_to_return_), &request_received_));
}

void FakeNetworkService::GetCookieStore(mx::channel cookie_store) {
  FTL_DCHECK(false);
}

void FakeNetworkService::CreateWebSocket(mx::channel socket) {
  FTL_DCHECK(false);
}

void FakeNetworkService::CreateTCPBoundSocket(
    network::NetAddressPtr local_address,
    mx::channel bound_socket,
    const CreateTCPBoundSocketCallback& callback) {
  FTL_DCHECK(false);
}

void FakeNetworkService::CreateTCPConnectedSocket(
    network::NetAddressPtr remote_address,
    mx::datapipe_consumer send_stream,
    mx::datapipe_producer receive_stream,
    mx::channel client_socket,
    const CreateTCPConnectedSocketCallback& callback) {
  FTL_DCHECK(false);
}

void FakeNetworkService::CreateUDPSocket(mx::channel socket) {
  FTL_DCHECK(false);
}

void FakeNetworkService::CreateHttpServer(
    network::NetAddressPtr local_address,
    mx::channel delegate,
    const CreateHttpServerCallback& callback) {
  FTL_DCHECK(false);
}

void FakeNetworkService::RegisterURLLoaderInterceptor(mx::channel factory) {
  FTL_DCHECK(false);
}

void FakeNetworkService::CreateHostResolver(mx::channel host_resolver) {
  FTL_DCHECK(false);
}

}  // namespace fake_network_service
