// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TEST_LEDGER_APP_INSTANCE_FACTORY_H_
#define APPS_LEDGER_SRC_TEST_LEDGER_APP_INSTANCE_FACTORY_H_

#include <memory>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/fidl_helpers/bound_interface_set.h"
#include "apps/ledger/src/test/fake_token_provider.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace test {

// Base class for client tests.
//
// Client tests are tests that act as clients to the Ledger as a whole. These
// are integration tests or end-to-end tests (apptests).
class LedgerAppInstanceFactory {
 public:
  // A Ledger app instance
  class LedgerAppInstance {
   public:
    LedgerAppInstance(
        ledger::FirebaseConfigPtr firebase_config,
        fidl::Array<uint8_t> test_ledger_name,
        ledger::LedgerRepositoryFactoryPtr ledger_repository_factory,
        fxl::RefPtr<fxl::TaskRunner> services_task_runner);
    virtual ~LedgerAppInstance();

    // Returns the LedgerRepositoryFactory associated with this application
    // instance.
    ledger::LedgerRepositoryFactory* ledger_repository_factory();
    // Builds and returns a new connection to the default LedgerRepository
    // object.
    ledger::LedgerRepositoryPtr GetTestLedgerRepository();
    // Erases the default ledger repository.
    void EraseTestLedgerRepository();
    // Builds and returns a new connection to the default Ledger object.
    ledger::LedgerPtr GetTestLedger();
    // Builds and returns a new connection to a new random page on the default
    // Ledger object.
    ledger::PagePtr GetTestPage();
    // Returns a connection to the given page on the default Ledger object.
    ledger::PagePtr GetPage(const fidl::Array<uint8_t>& page_id,
                            ledger::Status expected_status);
    // Deletes the given page on the default Ledger object.
    void DeletePage(const fidl::Array<uint8_t>& page_id,
                    ledger::Status expected_status);
    // Unbinds current connections to the token provider.
    void UnbindTokenProvider();

   private:
    ledger::FirebaseConfigPtr firebase_config_;
    fidl::Array<uint8_t> test_ledger_name_;
    ledger::LedgerRepositoryFactoryPtr ledger_repository_factory_;
    ledger::fidl_helpers::BoundInterfaceSet<modular::auth::TokenProvider,
                                            FakeTokenProvider>
        token_provider_impl_;
    fxl::RefPtr<fxl::TaskRunner> services_task_runner_;

    files::ScopedTempDir dir_;

    FXL_DISALLOW_COPY_AND_ASSIGN(LedgerAppInstance);
  };

  LedgerAppInstanceFactory() {}
  virtual ~LedgerAppInstanceFactory() {}

  // Sets a custom server id for synchronization.
  virtual void SetServerId(std::string server_id) = 0;

  virtual std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance() = 0;
};

std::unique_ptr<LedgerAppInstanceFactory> GetLedgerAppInstanceFactory();

}  // namespace test

#endif  // APPS_LEDGER_SRC_TEST_LEDGER_APP_INSTANCE_FACTORY_H_
