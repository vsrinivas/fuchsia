// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_FACTORY_IMPL_H_
#define SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_FACTORY_IMPL_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/cloud/firestore/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/callback/auto_cleanable.h>
#include <lib/callback/cancellable.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/memory/ref_ptr.h>

#include "peridot/lib/rng/random.h"
#include "src/ledger/cloud_provider_firestore/bin/app/cloud_provider_impl.h"

namespace cloud_provider_firestore {

class FactoryImpl : public Factory {
 public:
  explicit FactoryImpl(async_dispatcher_t* dispatcher, rng::Random* random,
                       component::StartupContext* startup_context,
                       std::string cobalt_client_name);

  ~FactoryImpl() override;

  // Shuts down all cloud providers owned by the class.
  //
  // It is only valid to delete the factory after the completion callback is
  // called.
  void ShutDown(fit::closure callback);

 private:
  // Factory:
  void GetCloudProvider(
      Config config,
      fidl::InterfaceHandle<fuchsia::auth::TokenManager> token_manager,
      fidl::InterfaceRequest<cloud_provider::CloudProvider>
          cloud_provider_request,
      GetCloudProviderCallback callback) override;

  void GetFirebaseCloudProvider(
      Config config,
      std::unique_ptr<firebase_auth::FirebaseAuthImpl> firebase_auth,
      fidl::InterfaceRequest<cloud_provider::CloudProvider>
          cloud_provider_request,
      fit::function<void(cloud_provider::Status)> callback);

  async_dispatcher_t* const dispatcher_;
  rng::Random* random_;
  component::StartupContext* const startup_context_;
  const std::string cobalt_client_name_;
  callback::CancellableContainer token_requests_;
  callback::AutoCleanableSet<CloudProviderImpl> providers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FactoryImpl);
};

}  // namespace cloud_provider_firestore

#endif  // SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_FACTORY_IMPL_H_
