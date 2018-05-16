// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_AUTH_TOKEN_MANAGER_TEST_FACTORY_IMPL_H_
#define GARNET_BIN_AUTH_TOKEN_MANAGER_TEST_FACTORY_IMPL_H_

#include <unordered_set>

#include <auth/cpp/fidl.h>

#include "garnet/bin/auth/token_manager/test/dev_auth_provider_impl.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"

namespace auth {
namespace dev_auth_provider {

class FactoryImpl : public auth::AuthProviderFactory {
 public:
  FactoryImpl() {}

  ~FactoryImpl() override {}

 private:
  // Factory:
  void GetAuthProvider(fidl::InterfaceRequest<auth::AuthProvider> auth_provider,
                       GetAuthProviderCallback callback) override;

  fidl::BindingSet<
      AuthProvider,
      std::unique_ptr<auth::dev_auth_provider::DevAuthProviderImpl>>
      dev_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FactoryImpl);
};

}  // namespace dev_auth_provider
}  // namespace auth

#endif  // GARNET_BIN_AUTH_TOKEN_MANAGER_TEST_FACTORY_IMPL_H_
