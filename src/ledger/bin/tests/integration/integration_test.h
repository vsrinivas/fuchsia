// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTS_INTEGRATION_INTEGRATION_TEST_H_
#define SRC_LEDGER_BIN_TESTS_INTEGRATION_INTEGRATION_TEST_H_

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/gtest/real_loop_fixture.h>

#include <functional>

#include <trace-provider/provider.h>

#include "gtest/gtest.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/testing/ledger_app_instance_factory.h"

namespace ledger {

// Base class for integration tests.
//
// Integration tests verify interactions with client-facing FIDL services
// exposed by Ledger. The FIDL services are run within the test process, on a
// separate thread.
class BaseIntegrationTest : public ::testing::Test, public LoopController {
 public:
  explicit BaseIntegrationTest(const LedgerAppInstanceFactoryBuilder* factory_builder);
  ~BaseIntegrationTest() override;

  BaseIntegrationTest(const BaseIntegrationTest&) = delete;
  BaseIntegrationTest(BaseIntegrationTest&&) = delete;
  BaseIntegrationTest& operator=(const BaseIntegrationTest&) = delete;
  BaseIntegrationTest& operator=(BaseIntegrationTest&&) = delete;

  // LoopController:
  void RunLoop() override;
  void StopLoop() override;
  std::unique_ptr<SubLoop> StartNewLoop() override;
  std::unique_ptr<CallbackWaiter> NewWaiter() override;
  async_dispatcher_t* dispatcher() override;
  bool RunLoopUntil(fit::function<bool()> condition) override;
  void RunLoopFor(zx::duration duration) override;

 protected:
  // ::testing::Test:
  void SetUp() override;
  void TearDown() override;

  zx::socket StreamDataToSocket(std::string data);

  std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance> NewLedgerAppInstance();

  LedgerAppInstanceFactory* GetAppFactory();

  LoopController* GetLoopController();

  rng::Random* GetRandom();

 private:
  const LedgerAppInstanceFactoryBuilder* factory_builder_;
  std::unique_ptr<LedgerAppInstanceFactory> factory_;
  // Loop used to run network service and token provider tasks.
  std::unique_ptr<SubLoop> services_loop_;
  std::unique_ptr<trace::TraceProviderWithFdio> trace_provider_;
};

class IntegrationTest
    : public BaseIntegrationTest,
      public ::testing::WithParamInterface<const LedgerAppInstanceFactoryBuilder*> {
 public:
  IntegrationTest();
  ~IntegrationTest() override;
};

// Initializes test environment based on the command line arguments.
//
// Returns true iff the initialization was successful.
bool ProcessCommandLine(int argc, char** argv);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTS_INTEGRATION_INTEGRATION_TEST_H_
