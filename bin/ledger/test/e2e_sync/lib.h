// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TEST_E2E_SYNC_LIB_H_
#define APPS_LEDGER_SRC_TEST_E2E_SYNC_LIB_H_

#include <functional>
#include <memory>

#include "application/lib/app/application_context.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/fidl_helpers/bound_interface_set.h"
#include "apps/ledger/src/test/app_test.h"
#include "apps/ledger/src/test/fake_token_provider.h"
#include "apps/ledger/src/test/get_ledger.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"

namespace test {
namespace e2e_sync {

// Base test class for synchronization tests. Other tests should derive from
// this class to use the proper synchronization configuration.
class SyncTest : public ::test::AppTest {
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

  SyncTest();
  ~SyncTest() override;

  std::unique_ptr<LedgerPtrHolder> GetLedger(std::string ledger_name,
                                             test::Erase erase);

 protected:
  void SetUp() override;

 private:
  ledger::fidl_helpers::BoundInterfaceSet<modular::auth::TokenProvider,
                                          test::FakeTokenProvider>
      token_provider_impl_;
};
}  // namespace e2e_sync
}  // namespace test

#endif  // APPS_LEDGER_SRC_TEST_E2E_SYNC_LIB_H_
