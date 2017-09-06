// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider_firebase/factory_impl.h"

#include "garnet/public/lib/ftl/logging.h"

namespace cloud_provider_firebase {

FactoryImpl::FactoryImpl(ftl::RefPtr<ftl::TaskRunner> main_runner)
    : main_runner_(std::move(main_runner)) {}

FactoryImpl::~FactoryImpl() {}

void FactoryImpl::GetCloudProvider(
    ConfigPtr config,
    fidl::InterfaceHandle<modular::auth::TokenProvider> token_provider,
    fidl::InterfaceRequest<cloud_provider::CloudProvider> cloud_provider,
    const GetCloudProviderCallback& callback) {
  providers_.emplace(main_runner_, std::move(cloud_provider), std::move(config),
                     std::move(token_provider));
  callback(cloud_provider::Status::OK);
}

}  // namespace cloud_provider_firebase
