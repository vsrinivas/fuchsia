// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider_firebase/factory_impl.h"

#include "garnet/public/lib/ftl/logging.h"

namespace cloud_provider_firebase {

FactoryImpl::FactoryImpl() {}

FactoryImpl::~FactoryImpl() {}

void FactoryImpl::GetCloudProvider(
    ConfigPtr /*config*/,
    fidl::InterfaceHandle<modular::auth::TokenProvider> /*token_provider*/,
    fidl::InterfaceRequest<cloud_provider::CloudProvider> /*cloud_provider*/,
    const GetCloudProviderCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

}  // namespace cloud_provider_firebase
