// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_LEDGER_APP_INSTANCE_FACTORY_H_
#define PERIDOT_BIN_LEDGER_TESTING_LEDGER_APP_INSTANCE_FACTORY_H_

#include <functional>
#include <memory>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/memory/ref_ptr.h>

#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace ledger {

// Base class for client tests.
//
// Client tests are tests that act as clients to the Ledger as a whole. These
// are integration tests or end-to-end tests (apptests).
class LedgerAppInstanceFactory {
 public:
  // Helper class for waiting for asynchronous event.
  // For a given |CallbackWaiter|, one can retrieve a callback through
  // |GetCallback|. The callback must be called when the asynchronous event
  // ends.
  // When |RunUntilCalled| is called, it will run the event loop, until the
  // callback from |GetCallback| is called.
  // If one is waiting for the callback to be called multiple times, one can
  // execute |RunUntilCalled| multiple times. The |n|th run of |RunUntilCalled|
  // will return once the callback have been called at least |n| time.
  // |GetCallback| can be called multiple time, and all the returned callback
  // will be equivalent.
  class CallbackWaiter {
   public:
    CallbackWaiter() {}
    virtual ~CallbackWaiter() {}
    virtual fit::function<void()> GetCallback() = 0;
    virtual void RunUntilCalled() = 0;
    // Returns whether the next expected calback has not already been called. If
    // |false|, |RunUntilCalled| will return immediately.
    virtual bool NotCalledYet() = 0;
  };

  // Controller for the main run loop. This allows to control the loop that will
  // call the factory and the multiple instances.
  class LoopController {
   public:
    // Runs the loop.
    virtual void RunLoop() = 0;
    // Stops the loop.
    virtual void StopLoop() = 0;
    // Returns a waiter that can be used to run the loop until a callback has
    // been called.
    std::unique_ptr<CallbackWaiter> NewWaiter();
  };
  // A Ledger app instance
  class LedgerAppInstance {
   public:
    LedgerAppInstance(
        LoopController* loop_controller,
        fidl::VectorPtr<uint8_t> test_ledger_name,
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
    PagePtr GetPage(const PageIdPtr& page_id, Status expected_status);

   private:
    virtual cloud_provider::CloudProviderPtr MakeCloudProvider() = 0;

    LoopController* loop_controller_;
    fidl::VectorPtr<uint8_t> test_ledger_name_;
    ledger_internal::LedgerRepositoryFactoryPtr ledger_repository_factory_;

    scoped_tmpfs::ScopedTmpFS tmpfs_;

    FXL_DISALLOW_COPY_AND_ASSIGN(LedgerAppInstance);
  };

  LedgerAppInstanceFactory() {}
  virtual ~LedgerAppInstanceFactory() {}

  // Starts a new instance of the Ledger. The |loop_controller| must allow to
  // control the loop that is used to access the LedgerAppInstance.
  virtual std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance(
      LoopController* loop_controller) = 0;
};

std::vector<LedgerAppInstanceFactory*> GetLedgerAppInstanceFactories();

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_LEDGER_APP_INSTANCE_FACTORY_H_
