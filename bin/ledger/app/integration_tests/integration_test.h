// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_INTEGRATION_TESTS_INTEGRATION_TEST_H_
#define APPS_LEDGER_SRC_APP_INTEGRATION_TESTS_INTEGRATION_TEST_H_

#include <functional>
#include <thread>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/test/cloud_server/fake_cloud_network_service.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"

namespace ledger {
namespace integration_tests {

// Base class for integration tests.
//
// Integration tests verify interactions with client-facing FIDL services
// exposed by Ledger. The FIDL services are run within the test process, on a
// separate thread.
class IntegrationTest : public test::TestWithMessageLoop {
 public:
  class LedgerAppInstance {
   public:
    LedgerAppInstance() {}
    virtual ~LedgerAppInstance() {}

    // Returns the LedgerRepositoryFactory associated with this application
    // instance.
    virtual LedgerRepositoryFactory* ledger_repository_factory() = 0;
    // Returns a default Ledger object.
    virtual Ledger* ledger() = 0;
    // Builds and returns a new connection to the default LedgerRepository
    // object.
    virtual LedgerRepositoryPtr GetTestLedgerRepository() = 0;
    // Builds and returns a new connection to the default Ledger object.
    virtual LedgerPtr GetTestLedger() = 0;
    // Builds and returns a new connection to a new random page on the default
    // Ledger object.
    virtual PagePtr GetTestPage() = 0;
    // Returns a connection to the given page on the default Ledger object.
    virtual PagePtr GetPage(const fidl::Array<uint8_t>& page_id,
                            Status expected_status) = 0;
    // Deletes the given page on the default Ledger object.
    virtual void DeletePage(const fidl::Array<uint8_t>& page_id,
                            Status expected_status) = 0;
    // Unbinds current connections to the token provider.
    virtual void UnbindTokenProvider() = 0;

   private:
    FTL_DISALLOW_COPY_AND_ASSIGN(LedgerAppInstance);
  };

  IntegrationTest() {}
  ~IntegrationTest() override {}

 protected:
  // ::testing::Test:
  void SetUp() override;
  void TearDown() override;

  mx::socket StreamDataToSocket(std::string data);

  LedgerRepositoryFactory* ledger_repository_factory() {
    return default_instance_->ledger_repository_factory();
  }

  Ledger* ledger() { return default_instance_->ledger(); }

  LedgerRepositoryPtr GetTestLedgerRepository() {
    return default_instance_->GetTestLedgerRepository();
  }

  LedgerPtr GetTestLedger() { return default_instance_->GetTestLedger(); }

  PagePtr GetTestPage() { return default_instance_->GetTestPage(); }

  PagePtr GetPage(const fidl::Array<uint8_t>& page_id, Status expected_status) {
    return default_instance_->GetPage(page_id, expected_status);
  }

  void DeletePage(const fidl::Array<uint8_t>& page_id, Status expected_status) {
    default_instance_->DeletePage(page_id, expected_status);
  }

  void UnbindTokenProvider() { default_instance_->UnbindTokenProvider(); }

  std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance();

 private:
  // Thread used to do socket IOs.
  std::thread socket_thread_;
  // Thread used to run the network service and the token provider.
  std::thread services_thread_;
  ftl::RefPtr<ftl::TaskRunner> socket_task_runner_;
  ftl::RefPtr<ftl::TaskRunner> services_task_runner_;
  FakeCloudNetworkService network_service_;
  std::unique_ptr<LedgerAppInstance> default_instance_;

  FTL_DISALLOW_COPY_AND_ASSIGN(IntegrationTest);
};

}  // namespace integration_tests
}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_INTEGRATION_TESTS_INTEGRATION_TEST_H_
