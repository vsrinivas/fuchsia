// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_FAKE_NETWORK_SERVICE_FAKE_URL_LOADER_H_
#define APPS_LEDGER_FAKE_NETWORK_SERVICE_FAKE_URL_LOADER_H_

#include "mojo/public/cpp/bindings/binding.h"
#include "apps/network/interfaces/url_loader.mojom.h"
#include "lib/ftl/macros.h"

namespace fake_network_service {

// Url loader that stores the url request for inspection in |request_received|,
// and returns response indicated in |response_to_return|. |response_to_return|
// is moved out in ::Start().
class FakeURLLoader : public mojo::URLLoader {
 public:
  FakeURLLoader(mojo::InterfaceRequest<mojo::URLLoader> message_pipe,
                mojo::URLResponsePtr response_to_return,
                mojo::URLRequestPtr* request_received);
  ~FakeURLLoader() override;

  // URLLoader:
  void Start(mojo::URLRequestPtr request,
             const StartCallback& callback) override;
  void FollowRedirect(const FollowRedirectCallback& callback) override;
  void QueryStatus(const QueryStatusCallback& callback) override;

 private:
  mojo::Binding<mojo::URLLoader> binding_;
  mojo::URLResponsePtr response_to_return_;
  mojo::URLRequestPtr* request_received_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FakeURLLoader);
};

}  // namespace fake_network_service

#endif  // APPS_LEDGER_FAKE_NETWORK_SERVICE_FAKE_URL_LOADER_H_
