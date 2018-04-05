// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FIREBASE_FACTORY_H_
#define PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FIREBASE_FACTORY_H_

#include <thread>

#include <fuchsia/cpp/cloud_provider.h>
#include <fuchsia/cpp/cloud_provider_firebase.h>
#include <lib/async/cpp/loop.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "peridot/bin/ledger/fidl_helpers/bound_interface_set.h"
#include "peridot/lib/firebase_auth/testing/fake_token_provider.h"

namespace test {

// Manager for real cloud provider backed by fake token provider.
//
// This is used to configure Ledger for end-to-end tests and benchmarks that use
// the real cloud provider.
class CloudProviderFirebaseFactory {
 public:
  explicit CloudProviderFirebaseFactory(
      component::ApplicationContext* application_context);
  ~CloudProviderFirebaseFactory();

  void Init();

  void MakeCloudProvider(
      std::string server_id,
      std::string api_key,
      fidl::InterfaceRequest<cloud_provider::CloudProvider> request);

 private:
  component::ApplicationContext* application_context_;

  // Thread used to run the fake token manager on.
  async::Loop loop_;

  ledger::fidl_helpers::BoundInterfaceSet<modular_auth::TokenProvider,
                                          firebase_auth::FakeTokenProvider>
      token_provider_;

  component::ApplicationControllerPtr cloud_provider_controller_;
  cloud_provider_firebase::FactoryPtr cloud_provider_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CloudProviderFirebaseFactory);
};

}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FIREBASE_FACTORY_H_
