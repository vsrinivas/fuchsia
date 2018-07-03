// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FIREBASE_FACTORY_H_
#define PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FIREBASE_FACTORY_H_

#include <thread>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/cloud/firebase/cpp/fidl.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fxl/memory/ref_ptr.h>

#include "peridot/bin/ledger/fidl/include/types.h"
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
      fuchsia::sys::StartupContext* startup_context);
  ~CloudProviderFirebaseFactory();

  void Init();

  void MakeCloudProvider(
      std::string server_id, std::string api_key,
      fidl::InterfaceRequest<cloud_provider::CloudProvider> request);

 private:
  fuchsia::sys::StartupContext* startup_context_;

  // Thread used to run the fake token manager on.
  async::Loop loop_;

  ledger::fidl_helpers::BoundInterfaceSet<fuchsia::modular::auth::TokenProvider,
                                          firebase_auth::FakeTokenProvider>
      token_provider_;

  fuchsia::sys::ComponentControllerPtr cloud_provider_controller_;
  fuchsia::ledger::cloud::firebase::FactoryPtr cloud_provider_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CloudProviderFirebaseFactory);
};

}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FIREBASE_FACTORY_H_
