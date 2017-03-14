// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_AUTH_TOKEN_PROVDER_IMPL_H_
#define APPS_MODULAR_LIB_AUTH_TOKEN_PROVDER_IMPL_H_

#include "apps/modular/services/auth/token_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {

class TokenProviderImpl : public TokenProvider {
 public:
  TokenProviderImpl(const std::string& auth_token) : auth_token_(auth_token) {}

  ~TokenProviderImpl() override {}

  void AddBinding(fidl::InterfaceRequest<TokenProvider> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  void GetAuthToken(const GetAuthTokenCallback& callback) override {
    callback(auth_token_);
  }

  std::string auth_token_;
  fidl::BindingSet<TokenProvider> bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TokenProviderImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_AUTH_TOKEN_PROVDER_IMPL_H_
