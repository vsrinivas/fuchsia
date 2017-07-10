// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/cloud_server/fake_cloud_network_service.h"

#include "lib/ftl/macros.h"

namespace ledger {

FakeCloudNetworkService::FakeCloudNetworkService() {}

FakeCloudNetworkService::~FakeCloudNetworkService() {}

void FakeCloudNetworkService::CreateURLLoader(
    ::fidl::InterfaceRequest<network::URLLoader> loader) {
  loader_bindings_.AddBinding(&url_loader_, std::move(loader));
}

void FakeCloudNetworkService::GetCookieStore(mx::channel cookie_store) {
  FTL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateWebSocket(mx::channel socket) {
  FTL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateTCPBoundSocket(
    network::NetAddressPtr local_address,
    mx::channel bound_socket,
    const CreateTCPBoundSocketCallback& callback) {
  FTL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateTCPConnectedSocket(
    network::NetAddressPtr remote_address,
    mx::socket send_stream,
    mx::socket receive_stream,
    mx::channel client_socket,
    const CreateTCPConnectedSocketCallback& callback) {
  FTL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateUDPSocket(mx::channel socket) {
  FTL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateHttpServer(
    network::NetAddressPtr local_address,
    mx::channel delegate,
    const CreateHttpServerCallback& callback) {
  FTL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::RegisterURLLoaderInterceptor(
    mx::channel factory) {
  FTL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateHostResolver(mx::channel host_resolver) {
  FTL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::AddBinding(
    fidl::InterfaceRequest<network::NetworkService> request) {
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace ledger
