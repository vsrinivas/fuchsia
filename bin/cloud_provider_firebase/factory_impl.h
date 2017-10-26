// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_FACTORY_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_FACTORY_IMPL_H_

#include "lib/auth/fidl/token_provider.fidl.h"
#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/cloud_provider_firebase/cloud_provider_impl.h"
#include "peridot/bin/cloud_provider_firebase/fidl/factory.fidl.h"
#include "peridot/bin/cloud_provider_firebase/network/network_service.h"
#include "peridot/bin/ledger/callback/auto_cleanable.h"
#include "peridot/bin/ledger/callback/cancellable.h"

namespace cloud_provider_firebase {

class FactoryImpl : public Factory {
 public:
  explicit FactoryImpl(fxl::RefPtr<fxl::TaskRunner> main_runner,
                       ledger::NetworkService* network_service);

  ~FactoryImpl() override;

 private:
  // Factory:
  void GetCloudProvider(
      ConfigPtr config,
      fidl::InterfaceHandle<modular::auth::TokenProvider> token_provider,
      fidl::InterfaceRequest<cloud_provider::CloudProvider> cloud_provider,
      const GetCloudProviderCallback& callback) override;

  bool IsEmpty();

  void CheckEmpty();

  fxl::RefPtr<fxl::TaskRunner> main_runner_;
  ledger::NetworkService* const network_service_;
  callback::CancellableContainer token_requests_;
  callback::AutoCleanableSet<CloudProviderImpl> providers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FactoryImpl);
};

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_FACTORY_IMPL_H_
