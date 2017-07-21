// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_SYNC_TESTS_LIB_H_
#define APPS_LEDGER_SRC_SYNC_TESTS_LIB_H_

#include <functional>
#include <memory>

#include "application/lib/app/application_context.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/fidl_helpers/bound_interface_set.h"
#include "apps/ledger/src/test/fake_token_provider.h"
#include "apps/ledger/src/test/get_ledger.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"

namespace sync_test {

// Base test class for synchronization tests. Other tests should derive from
// this class to use the proper synchronization configuration.
class SyncTestBase : public ::testing::Test {
 public:
  class LedgerPtrHolder {
   public:
    LedgerPtrHolder(files::ScopedTempDir dir,
                    app::ApplicationControllerPtr controller,
                    ledger::LedgerPtr ledger);
    ~LedgerPtrHolder();

    ledger::LedgerPtr ledger;

   private:
    files::ScopedTempDir dir_;
    app::ApplicationControllerPtr controller_;
    FTL_DISALLOW_COPY_AND_ASSIGN(LedgerPtrHolder);
  };

  SyncTestBase();
  virtual ~SyncTestBase();

  std::unique_ptr<LedgerPtrHolder> GetLedger(std::string ledger_name,
                                             test::Erase erase);

  // Runs the loop for at most |timeout|. Returns |true| if the timeout has been
  // reached.
  bool RunLoopWithTimeout(
      ftl::TimeDelta timeout = ftl::TimeDelta::FromSeconds(1));

  // Runs the loop until the condition returns true or the timeout is reached.
  // Returns |true| if the condition was met, and |false| if the timeout was
  // reached.
  bool RunLoopUntil(std::function<bool()> condition,
                    ftl::TimeDelta timeout = ftl::TimeDelta::FromSeconds(1));

  // Creates a closure that quits the test message loop when executed.
  ftl::Closure MakeQuitTask();

 protected:
  void SetUp() override;

  mtl::MessageLoop* message_loop_;

 private:
  ledger::fidl_helpers::BoundInterfaceSet<modular::auth::TokenProvider,
                                          test::FakeTokenProvider>
      token_provider_impl_;
};
}  // namespace sync_test

#endif  // APPS_LEDGER_SRC_SYNC_TESTS_LIB_H_
