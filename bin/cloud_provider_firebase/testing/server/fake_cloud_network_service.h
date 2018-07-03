// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_FAKE_CLOUD_NETWORK_SERVICE_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_FAKE_CLOUD_NETWORK_SERVICE_H_

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/cloud_provider_firebase/testing/server/fake_cloud_url_loader.h"

namespace ledger {

// Implementation of fuchsia::net::oldhttp::HttpService that simulates Firebase
// and GCS servers.
class FakeCloudNetworkService : public ::fuchsia::net::oldhttp::HttpService {
 public:
  FakeCloudNetworkService();
  ~FakeCloudNetworkService() override;

  // network::NetworkService
  void CreateURLLoader(
      ::fidl::InterfaceRequest<::fuchsia::net::oldhttp::URLLoader> loader)
      override;
  // Bind a new request to this implementation.
  void AddBinding(
      fidl::InterfaceRequest<::fuchsia::net::oldhttp::HttpService> request);

 private:
  FakeCloudURLLoader url_loader_;
  fidl::BindingSet<::fuchsia::net::oldhttp::URLLoader> loader_bindings_;
  fidl::BindingSet<::fuchsia::net::oldhttp::HttpService> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeCloudNetworkService);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_FAKE_CLOUD_NETWORK_SERVICE_H_
