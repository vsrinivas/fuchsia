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

void FakeCloudNetworkService::GetCookieStore(mx::channel /*cookie_store*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateWebSocket(mx::channel /*socket*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateTCPBoundSocket(
    netstack::NetAddressPtr /*local_address*/,
    mx::channel /*bound_socket*/,
    const CreateTCPBoundSocketCallback& /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateTCPConnectedSocket(
    netstack::NetAddressPtr /*remote_address*/,
    mx::socket /*send_stream*/,
    mx::socket /*receive_stream*/,
    mx::channel /*client_socket*/,
    const CreateTCPConnectedSocketCallback& /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateUDPSocket(mx::channel /*socket*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateHttpServer(
    netstack::NetAddressPtr /*local_address*/,
    mx::channel /*delegate*/,
    const CreateHttpServerCallback& /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::RegisterURLLoaderInterceptor(
    mx::channel /*factory*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::CreateHostResolver(
    mx::channel /*host_resolver*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudNetworkService::AddBinding(
    fidl::InterfaceRequest<network::NetworkService> request) {
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace ledger
