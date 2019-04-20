// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/identity/bin/google_auth_provider/factory_impl.h"

#include "src/identity/bin/google_auth_provider/settings.h"

namespace google_auth_provider {

FactoryImpl::FactoryImpl(async_dispatcher_t* main_dispatcher,
                         sys::ComponentContext* context,
                         network_wrapper::NetworkWrapper* network_wrapper,
                         Settings settings)
    : main_dispatcher_(main_dispatcher),
      context_(context),
      network_wrapper_(network_wrapper),
      settings_(std::move(settings)) {
  FXL_DCHECK(context_);
  FXL_DCHECK(network_wrapper_);
}

FactoryImpl::~FactoryImpl() {}

void FactoryImpl::Bind(
    fidl::InterfaceRequest<fuchsia::auth::AuthProviderFactory> request) {
  factory_bindings_.AddBinding(this, std::move(request));
}

void FactoryImpl::GetAuthProvider(
    fidl::InterfaceRequest<fuchsia::auth::AuthProvider> auth_provider,
    GetAuthProviderCallback callback) {
  providers_.emplace(main_dispatcher_, context_, network_wrapper_, settings_,
                     std::move(auth_provider));
  callback(fuchsia::auth::AuthProviderStatus::OK);
}

}  // namespace google_auth_provider
