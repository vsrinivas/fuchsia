// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_FAKE_NETWORK_SERVICE_FAKE_URL_LOADER_H_
#define APPS_LEDGER_SRC_FAKE_NETWORK_SERVICE_FAKE_URL_LOADER_H_

#include "apps/network/services/url_loader.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"

namespace fake_network_service {

// Url loader that stores the url request for inspection in |request_received|,
// and returns response indicated in |response_to_return|. |response_to_return|
// is moved out in ::Start().
class FakeURLLoader : public network::URLLoader {
 public:
  FakeURLLoader(fidl::InterfaceRequest<network::URLLoader> message_pipe,
                network::URLResponsePtr response_to_return,
                network::URLRequestPtr* request_received);
  ~FakeURLLoader() override;

  // URLLoader:
  void Start(network::URLRequestPtr request,
             const StartCallback& callback) override;
  void FollowRedirect(const FollowRedirectCallback& callback) override;
  void QueryStatus(const QueryStatusCallback& callback) override;

 private:
  fidl::Binding<network::URLLoader> binding_;
  network::URLResponsePtr response_to_return_;
  network::URLRequestPtr* request_received_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FakeURLLoader);
};

}  // namespace fake_network_service

#endif  // APPS_LEDGER_SRC_FAKE_NETWORK_SERVICE_FAKE_URL_LOADER_H_
