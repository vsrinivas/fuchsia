// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_INTEGRATION_TEST_H_
#define PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_INTEGRATION_TEST_H_

#include <functional>
#include <thread>

#include <trace-provider/provider.h>

#include <fuchsia/cpp/ledger.h>
#include <fuchsia/cpp/ledger_internal.h>
#include "garnet/lib/gtest/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/testing/ledger_app_instance_factory.h"

namespace test {
namespace integration {

// Base class for integration tests.
//
// Integration tests verify interactions with client-facing FIDL services
// exposed by Ledger. The FIDL services are run within the test process, on a
// separate thread.
class BaseIntegrationTest : public gtest::TestWithMessageLoop {
 public:
  BaseIntegrationTest();
  ~BaseIntegrationTest() override;

  BaseIntegrationTest(const BaseIntegrationTest&) = delete;
  BaseIntegrationTest(BaseIntegrationTest&&) = delete;
  BaseIntegrationTest& operator=(const BaseIntegrationTest&) = delete;
  BaseIntegrationTest& operator=(BaseIntegrationTest&&) = delete;

 protected:
  // ::testing::Test:
  void SetUp() override;
  void TearDown() override;

  zx::socket StreamDataToSocket(std::string data);

  std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
  NewLedgerAppInstance();

  virtual LedgerAppInstanceFactory* GetAppFactory() = 0;

 private:
  // Thread used to run the network service and the token provider.
  std::thread socket_thread_;
  fxl::RefPtr<fxl::TaskRunner> socket_task_runner_;

  std::unique_ptr<trace::TraceProvider> trace_provider_;
};

class IntegrationTest
    : public BaseIntegrationTest,
      public ::testing::WithParamInterface<LedgerAppInstanceFactory*> {
 public:
  IntegrationTest();
  ~IntegrationTest() override;

 protected:
  LedgerAppInstanceFactory* GetAppFactory() override;
};

// Initializes test environment based on the command line arguments.
//
// Returns true iff the initialization was successful.
bool ProcessCommandLine(int argc, char** argv);

}  // namespace integration
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_INTEGRATION_TEST_H_
