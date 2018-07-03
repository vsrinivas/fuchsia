// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/testing/server/fake_cloud_network_service.h"

#include <lib/fxl/macros.h>

namespace ledger {

namespace http = ::fuchsia::net::oldhttp;

FakeCloudNetworkService::FakeCloudNetworkService() {}

FakeCloudNetworkService::~FakeCloudNetworkService() {}

void FakeCloudNetworkService::CreateURLLoader(
    ::fidl::InterfaceRequest<http::URLLoader> loader) {
  loader_bindings_.AddBinding(&url_loader_, std::move(loader));
}

void FakeCloudNetworkService::AddBinding(
    fidl::InterfaceRequest<http::HttpService> request) {
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace ledger
