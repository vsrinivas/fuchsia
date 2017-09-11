// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TEST_INTEGRATION_SYNC_LIB_H_
#define APPS_LEDGER_SRC_TEST_INTEGRATION_SYNC_LIB_H_

#include <functional>
#include <memory>

#include "lib/app/cpp/application_context.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/fidl_helpers/bound_interface_set.h"
#include "apps/ledger/src/test/fake_token_provider.h"
#include "apps/ledger/src/test/get_ledger.h"
#include "apps/ledger/src/test/ledger_app_instance_factory.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"

namespace test {
namespace integration {
namespace sync {

// Base test class for synchronization tests. Other tests should derive from
// this class to use the proper synchronization configuration.
class SyncTest : public ::test::TestWithMessageLoop {
 public:
  SyncTest();
  ~SyncTest() override;

  std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
  NewLedgerAppInstance();

 protected:
  void SetUp() override;

 private:
  std::unique_ptr<LedgerAppInstanceFactory> app_factory_;
};

void ProcessCommandLine(int argc, char** argv);

}  // namespace sync
}  // namespace integration
}  // namespace test

#endif  // APPS_LEDGER_SRC_TEST_INTEGRATION_SYNC_LIB_H_
