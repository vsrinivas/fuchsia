// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TEST_CLOUD_PROVIDER_FIREBASE_FACTORY_H_
#define PERIDOT_BIN_LEDGER_TEST_CLOUD_PROVIDER_FIREBASE_FACTORY_H_

#include <thread>

#include "lib/app/cpp/application_context.h"
#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"
#include "peridot/bin/cloud_provider_firebase/fidl/factory.fidl.h"
#include "peridot/bin/ledger/convert/convert.h"
#include "peridot/bin/ledger/fidl_helpers/bound_interface_set.h"
#include "peridot/bin/ledger/test/fake_token_provider.h"

namespace test {

// Manager for real cloud provider backed by fake token provider.
//
// This is used to configure Ledger for end-to-end tests and benchmarks that use
// the real cloud provider.
class CloudProviderFirebaseFactory {
 public:
  explicit CloudProviderFirebaseFactory(
      app::ApplicationContext* application_context);
  ~CloudProviderFirebaseFactory();

  void Init();

  cloud_provider::CloudProviderPtr MakeCloudProvider(std::string server_id,
                                                     std::string api_key);

 private:
  app::ApplicationContext* application_context_;

  // Thread used to run the fake token manager on.
  std::thread services_thread_;
  fxl::RefPtr<fxl::TaskRunner> services_task_runner_;

  ledger::fidl_helpers::BoundInterfaceSet<modular::auth::TokenProvider,
                                          FakeTokenProvider>
      token_provider_;

  app::ApplicationControllerPtr cloud_provider_controller_;
  cloud_provider_firebase::FactoryPtr cloud_provider_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CloudProviderFirebaseFactory);
};

}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TEST_CLOUD_PROVIDER_FIREBASE_FACTORY_H_
