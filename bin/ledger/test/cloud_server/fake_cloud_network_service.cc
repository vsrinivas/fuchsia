// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/cloud_server/fake_cloud_network_service.h"

#include "lib/fxl/macros.h"

namespace ledger {

FakeCloudNetworkService::FakeCloudNetworkService() {}

FakeCloudNetworkService::~FakeCloudNetworkService() {}

void FakeCloudNetworkService::CreateURLLoader(
    ::fidl::InterfaceRequest<network::URLLoader> loader) {
  loader_bindings_.AddBinding(&url_loader_, std::move(loader));
}

void FakeCloudNetworkService::GetCookieStore(zx::channel /*cookie_store*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateWebSocket(zx::channel /*socket*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateTCPBoundSocket(
    netstack::NetAddressPtr /*local_address*/,
    zx::channel /*bound_socket*/,
    const CreateTCPBoundSocketCallback& /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateTCPConnectedSocket(
    netstack::NetAddressPtr /*remote_address*/,
    zx::socket /*send_stream*/,
    zx::socket /*receive_stream*/,
    zx::channel /*client_socket*/,
    const CreateTCPConnectedSocketCallback& /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateUDPSocket(zx::channel /*socket*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateHttpServer(
    netstack::NetAddressPtr /*local_address*/,
    zx::channel /*delegate*/,
    const CreateHttpServerCallback& /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::RegisterURLLoaderInterceptor(
    zx::channel /*factory*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateHostResolver(
    zx::channel /*host_resolver*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::AddBinding(
    fidl::InterfaceRequest<network::NetworkService> request) {
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace ledger
