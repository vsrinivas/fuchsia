// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_INTEGRATION_TEST_H_
#define PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_INTEGRATION_TEST_H_

#include <functional>

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fxl/macros.h>
#include <lib/gtest/real_loop_fixture.h>
#include <trace-provider/provider.h>

#include "gtest/gtest.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/testing/ledger_app_instance_factory.h"

namespace ledger {

// Base class for integration tests.
//
// Integration tests verify interactions with client-facing FIDL services
// exposed by Ledger. The FIDL services are run within the test process, on a
// separate thread.
class BaseIntegrationTest : public ::testing::Test, public LoopController {
 public:
  BaseIntegrationTest();
  ~BaseIntegrationTest() override;

  BaseIntegrationTest(const BaseIntegrationTest&) = delete;
  BaseIntegrationTest(BaseIntegrationTest&&) = delete;
  BaseIntegrationTest& operator=(const BaseIntegrationTest&) = delete;
  BaseIntegrationTest& operator=(BaseIntegrationTest&&) = delete;

  // LoopController:
  void RunLoop() override;
  void StopLoop() override;
  std::unique_ptr<SubLoop> StartNewLoop() override;
  async_dispatcher_t* dispatcher() override;
  fit::closure QuitLoopClosure() override;
  bool RunLoopUntil(fit::function<bool()> condition) override;
  bool RunLoopFor(zx::duration duration);

 protected:
  // ::testing::Test:
  void SetUp() override;
  void TearDown() override;

  zx::socket StreamDataToSocket(std::string data);

  std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
  NewLedgerAppInstance();

  virtual LedgerAppInstanceFactory* GetAppFactory() = 0;
  virtual LoopController* GetLoopController() = 0;

 private:
  // Loop used to run network service and token provider tasks.
  std::unique_ptr<SubLoop> services_loop_;
  std::unique_ptr<trace::TraceProvider> trace_provider_;
};

class IntegrationTest : public BaseIntegrationTest,
                        public ::testing::WithParamInterface<
                            const LedgerAppInstanceFactoryBuilder*> {
 public:
  IntegrationTest();
  ~IntegrationTest() override;

 protected:
  // ::testing::Test:
  void SetUp() override;

  // BaseIntegrationTest:
  LedgerAppInstanceFactory* GetAppFactory() override;
  LoopController* GetLoopController() override;

 private:
  std::unique_ptr<LedgerAppInstanceFactory> factory_;
  std::unique_ptr<LoopController> loop_controller_;
};

// Initializes test environment based on the command line arguments.
//
// Returns true iff the initialization was successful.
bool ProcessCommandLine(int argc, char** argv);

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_INTEGRATION_TEST_H_
