// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_TEST_WITH_LEDGER_H_
#define PERIDOT_LIB_TESTING_TEST_WITH_LEDGER_H_

#include <memory>

#include <lib/fit/function.h>

#include "lib/gtest/real_loop_fixture.h"
#include "peridot/lib/testing/ledger_repository_for_testing.h"

namespace modular {
class LedgerClient;

namespace testing {

// A test fixture class for a test case that needs a ledger repository, ledger,
// ledger client, or ledger page. This fixture sets up a ledger repository and a
// ledger client in SetUp() and erases the repository and stops the ledger in
// TearDown(). This also runs a message loop, which is required to interact with
// the ledger through fidl calls.
//
// The ledger client is available to the test case and its fixture through the
// ledger_client() getter, the ledger repository through ledger_repository().
//
// A fixture class that extends this fixture and has its own SetUp() and
// TearDown() must call this fixture's SetUp() and TearDown().
class TestWithLedger : public gtest::RealLoopFixture {
 public:
  TestWithLedger();
  ~TestWithLedger() override;

  void SetUp() override;
  void TearDown() override;

 protected:
  fuchsia::ledger::internal::LedgerRepository* ledger_repository() {
    return ledger_app_->ledger_repository();
  }
  LedgerClient* ledger_client() { return ledger_client_.get(); }

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
  std::unique_ptr<testing::LedgerRepositoryForTesting> ledger_app_;
  std::unique_ptr<LedgerClient> ledger_client_;
};

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_TEST_WITH_LEDGER_H_
