// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_LEDGER_APP_INSTANCE_FACTORY_H_
#define SRC_LEDGER_BIN_TESTING_LEDGER_APP_INSTANCE_FACTORY_H_

#include <functional>
#include <memory>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <src/lib/fxl/macros.h>

#include "peridot/lib/rng/random.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/testing/loop_controller.h"

namespace ledger {

class LedgerAppInstanceFactory;

// Class that creates instances of |LedgerAppInstanceFactory|.
class LedgerAppInstanceFactoryBuilder {
 public:
  virtual ~LedgerAppInstanceFactoryBuilder(){};
  // Returns a new LedgerAppInstanceFactory.
  virtual std::unique_ptr<LedgerAppInstanceFactory> NewFactory() const = 0;
};

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
        LoopController* loop_controller, std::vector<uint8_t> test_ledger_name,
        ledger_internal::LedgerRepositoryFactoryPtr ledger_repository_factory);
    virtual ~LedgerAppInstance();

    // Returns the LedgerRepositoryFactory associated with this application
    // instance.
    ledger_internal::LedgerRepositoryFactory* ledger_repository_factory();
    // Builds and returns a new connection to the default LedgerRepository
    // object.
    ledger_internal::LedgerRepositoryPtr GetTestLedgerRepository();
    // Builds and returns a new connection to the default Ledger object.
    LedgerPtr GetTestLedger();
    // Builds and returns a new connection to a new random page on the default
    // Ledger object.
    PagePtr GetTestPage();
    // Returns a connection to the given page on the default Ledger object.
    PagePtr GetPage(const PageIdPtr& page_id);

   private:
    virtual cloud_provider::CloudProviderPtr MakeCloudProvider() = 0;
    virtual std::string GetUserId() = 0;

    LoopController* loop_controller_;
    std::vector<uint8_t> test_ledger_name_;
    ledger_internal::LedgerRepositoryFactoryPtr ledger_repository_factory_;

    scoped_tmpfs::ScopedTmpFS tmpfs_;

    FXL_DISALLOW_COPY_AND_ASSIGN(LedgerAppInstance);
  };

  virtual ~LedgerAppInstanceFactory() {}

  // Starts a new instance of the Ledger. The |loop_controller| must allow to
  // control the loop that is used to access the LedgerAppInstance.
  virtual std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance() = 0;

  // Returns the Loop controller controlling the loops of the LedgerAppInstances
  // created by this.
  virtual LoopController* GetLoopController() = 0;

  // Returns a random instance to control the randomness of the test.
  virtual rng::Random* GetRandom() = 0;
};

// Returns the list of LedgerAppInstanceFactoryBuilder to be passed as
// parameters to the tests. The implementation of this function changes
// depending on whether the tests are ran as integration tests, or end to end
// tests.
std::vector<const LedgerAppInstanceFactoryBuilder*>
GetLedgerAppInstanceFactoryBuilders();

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_LEDGER_APP_INSTANCE_FACTORY_H_
