// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_LEDGER_APP_INSTANCE_FACTORY_H_
#define SRC_LEDGER_BIN_TESTING_LEDGER_APP_INSTANCE_FACTORY_H_

#include <fuchsia/inspect/deprecated/cpp/fidl.h>
#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr.h>

#include <functional>
#include <memory>

#include <gtest/gtest.h>

#include "peridot/lib/rng/random.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/testing/loop_controller.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/inspect_deprecated/hierarchy.h"

namespace ledger {

class LedgerAppInstanceFactory;

// Class that creates instances of |LedgerAppInstanceFactory|.
class LedgerAppInstanceFactoryBuilder {
 public:
  virtual ~LedgerAppInstanceFactoryBuilder(){};
  // Returns a new LedgerAppInstanceFactory.
  virtual std::unique_ptr<LedgerAppInstanceFactory> NewFactory() const = 0;

  // Prints the factory builder parameters.
  virtual std::string TestSuffix() const = 0;
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
    LedgerAppInstance(LoopController* loop_controller, std::vector<uint8_t> test_ledger_name,
                      ledger_internal::LedgerRepositoryFactoryPtr ledger_repository_factory,
                      fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> inspect_ptr);
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
    // Populates |hierarchy| with the results of an inspection of the Ledger app under test.
    bool Inspect(LoopController* loop_controller, inspect_deprecated::ObjectHierarchy* hierarchy);

   private:
    virtual cloud_provider::CloudProviderPtr MakeCloudProvider() = 0;
    virtual std::string GetUserId() = 0;

    LoopController* loop_controller_;
    std::vector<uint8_t> test_ledger_name_;
    ledger_internal::LedgerRepositoryFactoryPtr ledger_repository_factory_;
    fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> inspect_;

    scoped_tmpfs::ScopedTmpFS tmpfs_;

    FXL_DISALLOW_COPY_AND_ASSIGN(LedgerAppInstance);
  };

  virtual ~LedgerAppInstanceFactory() = default;

  // Starts a new instance of the Ledger. The |loop_controller| must allow to
  // control the loop that is used to access the LedgerAppInstance.
  virtual std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance() = 0;

  // Returns the Loop controller controlling the loops of the LedgerAppInstances
  // created by this.
  virtual LoopController* GetLoopController() = 0;

  // Returns a random instance to control the randomness of the test.
  virtual rng::Random* GetRandom() = 0;
};

// Whether tests should only be performed with cloud synchronization enabled, or whether P2P and
// offline/disconnected cases should be considered too.
enum class EnableSynchronization {
  // Cloud sync should be available.
  CLOUD_SYNC_ONLY,
  // Cloud or P2P sync should be available.
  SYNC_ONLY,
  // This should be tested without sync.
  OFFLINE_ONLY,
  // Offline cases should be considered.
  SYNC_OR_OFFLINE,
  // Offline cases should be considered. It's not necessary to test with both diffs enabled and
  // diffs disabled.
  SYNC_OR_OFFLINE_DIFFS_IRRELEVANT,
};

// Returns the list of LedgerAppInstanceFactoryBuilder to be passed as
// parameters to the tests. The implementation of this function changes
// depending on whether the tests are ran as integration tests, or end to end
// tests.
std::vector<const LedgerAppInstanceFactoryBuilder*> GetLedgerAppInstanceFactoryBuilders(
    EnableSynchronization sync_state = EnableSynchronization::SYNC_OR_OFFLINE);

// Use as the third parameter of INSTANTIATE_TEST_SUITE_P to pretty-print a test suite parametrized
// with LedgerAppInstanceFactoryBuilder pointers as returned by GetLedgerAppInstanceFactoryBuilders.
struct PrintLedgerAppInstanceFactoryBuilder {
  template <class ParamType>
  std::string operator()(const ::testing::TestParamInfo<ParamType>& info) const {
    return info.param->TestSuffix();
  }
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_LEDGER_APP_INSTANCE_FACTORY_H_
