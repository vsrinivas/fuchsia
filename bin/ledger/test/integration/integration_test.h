// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TEST_INTEGRATION_INTEGRATION_TEST_H_
#define APPS_LEDGER_SRC_TEST_INTEGRATION_INTEGRATION_TEST_H_

#include <functional>
#include <thread>

#include "peridot/bin/ledger/fidl/internal.fidl.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/test/cloud_server/fake_cloud_network_service.h"
#include "peridot/bin/ledger/test/ledger_app_instance_factory.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fxl/macros.h"

namespace test {
namespace integration {

// Base class for integration tests.
//
// Integration tests verify interactions with client-facing FIDL services
// exposed by Ledger. The FIDL services are run within the test process, on a
// separate thread.
class IntegrationTest : public test::TestWithMessageLoop {
 public:
  IntegrationTest() {}
  ~IntegrationTest() override {}

 protected:
  // ::testing::Test:
  void SetUp() override;
  void TearDown() override;

  zx::socket StreamDataToSocket(std::string data);

  std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
  NewLedgerAppInstance();

 private:
  // Thread used to run the network service and the token provider.
  std::thread socket_thread_;
  fxl::RefPtr<fxl::TaskRunner> socket_task_runner_;

  std::unique_ptr<LedgerAppInstanceFactory> app_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(IntegrationTest);
};

}  // namespace integration
}  // namespace test

#endif  // APPS_LEDGER_SRC_TEST_INTEGRATION_INTEGRATION_TEST_H_
