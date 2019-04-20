// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_IDENTITY_BIN_GOOGLE_AUTH_PROVIDER_FACTORY_IMPL_H_
#define SRC_IDENTITY_BIN_GOOGLE_AUTH_PROVIDER_FACTORY_IMPL_H_

#include <fuchsia/auth/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include "lib/callback/auto_cleanable.h"
#include "lib/network_wrapper/network_wrapper.h"
#include "src/identity/bin/google_auth_provider/google_auth_provider_impl.h"
#include "src/identity/bin/google_auth_provider/settings.h"
#include "src/lib/fxl/macros.h"

namespace google_auth_provider {

class FactoryImpl : public fuchsia::auth::AuthProviderFactory {
 public:
  FactoryImpl(async_dispatcher_t* main_dispatcher,
              sys::ComponentContext* context,
              network_wrapper::NetworkWrapper* network_wrapper,
              Settings settings);

  ~FactoryImpl() override;

  void Bind(fidl::InterfaceRequest<fuchsia::auth::AuthProviderFactory> request);

 private:
  // Factory:
  void GetAuthProvider(
      fidl::InterfaceRequest<fuchsia::auth::AuthProvider> auth_provider,
      GetAuthProviderCallback callback) override;

  async_dispatcher_t* const main_dispatcher_;
  sys::ComponentContext* const context_;
  network_wrapper::NetworkWrapper* const network_wrapper_;
  const Settings settings_;

  callback::AutoCleanableSet<GoogleAuthProviderImpl> providers_;

  fidl::BindingSet<fuchsia::auth::AuthProviderFactory> factory_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FactoryImpl);
};

}  // namespace google_auth_provider

#endif  // SRC_IDENTITY_BIN_GOOGLE_AUTH_PROVIDER_FACTORY_IMPL_H_
