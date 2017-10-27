// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/test/server/fake_cloud_network_service.h"

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

void FakeCloudNetworkService::AddBinding(
    fidl::InterfaceRequest<network::NetworkService> request) {
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace ledger
