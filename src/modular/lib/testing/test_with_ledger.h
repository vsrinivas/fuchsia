// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_TESTING_TEST_WITH_LEDGER_H_
#define SRC_MODULAR_LIB_TESTING_TEST_WITH_LEDGER_H_

#include <lib/fit/function.h>
#include <lib/gtest/real_loop_fixture.h>

#include <memory>

#include "src/modular/lib/ledger_client/ledger_client.h"
#include "src/modular/lib/testing/ledger_repository_for_testing.h"

namespace modular_testing {

// ledger client, or ledger page. This runs a message loop, which is required to
// A test fixture class for a test case that needs a ledger repository, ledger,
// interact with the ledger through fidl calls.
//
// The ledger client is available to the test case and its fixture through the
// ledger_client() getter, the ledger repository through ledger_repository().
// If multiple connection to the same ledger is necessary, and new connection
// can be created with |NewLedgerClient|.
class TestWithLedger : public gtest::RealLoopFixture {
 public:
  TestWithLedger();
  ~TestWithLedger() override;

 protected:
  fuchsia::ledger::internal::LedgerRepository* ledger_repository() {
    return ledger_app_->ledger_repository();
  }
  modular::LedgerClient* ledger_client() { return ledger_client_.get(); }

  // Build a new LedgerClient connecting to the same underlying ledger.
  // This class must outlive the resulting client.
  std::unique_ptr<modular::LedgerClient> NewLedgerClient();

  // Increases default timeout over plain test with message loop, because
  // methods executing on the message loop are real fidl calls.
  //
  // Test cases involving ledger calls take about 300ms when running in CI.
  // Occasionally, however, they take much longer, presumably because of load on
  // shared machines. With the default timeout of RealLoopFixture of 1s, we
  // see flakiness. Cf. FW-287.
  bool RunLoopWithTimeout(zx::duration timeout = zx::sec(10));
  bool RunLoopWithTimeoutOrUntil(fit::function<bool()> condition,
                                 zx::duration timeout = zx::sec(10));

 private:
  std::unique_ptr<modular_testing::LedgerRepositoryForTesting> ledger_app_;
  std::unique_ptr<modular::LedgerClient> ledger_client_;
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_TESTING_TEST_WITH_LEDGER_H_
