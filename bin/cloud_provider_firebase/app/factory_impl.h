// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_APP_FACTORY_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_APP_FACTORY_IMPL_H_

#include "garnet/lib/callback/auto_cleanable.h"
#include "garnet/lib/callback/cancellable.h"
#include "lib/auth/fidl/token_provider.fidl.h"
#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/cloud_provider_firebase/app/cloud_provider_impl.h"
#include "peridot/bin/cloud_provider_firebase/fidl/factory.fidl.h"
#include "garnet/lib/network_wrapper/network_wrapper.h"

namespace cloud_provider_firebase {

class FactoryImpl : public Factory {
 public:
  explicit FactoryImpl(fxl::RefPtr<fxl::TaskRunner> main_runner,
                       network_wrapper::NetworkWrapper* network_wrapper);

  ~FactoryImpl() override;

 private:
  // Factory:
  void GetCloudProvider(
      ConfigPtr config,
      f1dl::InterfaceHandle<modular::auth::TokenProvider> token_provider,
      f1dl::InterfaceRequest<cloud_provider::CloudProvider> cloud_provider,
      const GetCloudProviderCallback& callback) override;

  fxl::RefPtr<fxl::TaskRunner> main_runner_;
  network_wrapper::NetworkWrapper* const network_wrapper_;
  callback::CancellableContainer token_requests_;
  callback::AutoCleanableSet<CloudProviderImpl> providers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FactoryImpl);
};

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_APP_FACTORY_IMPL_H_
