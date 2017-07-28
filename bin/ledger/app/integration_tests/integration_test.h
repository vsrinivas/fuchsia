// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_INTEGRATION_TESTS_INTEGRATION_TEST_H_
#define APPS_LEDGER_SRC_APP_INTEGRATION_TESTS_INTEGRATION_TEST_H_

#include <functional>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/app/erase_remote_repository_operation.h"
#include "apps/ledger/src/app/ledger_repository_factory_impl.h"
#include "apps/ledger/src/network/fake_network_service.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"
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

   private:
    FTL_DISALLOW_COPY_AND_ASSIGN(LedgerAppInstance);
  };

  IntegrationTest() {}
  virtual ~IntegrationTest() override {}

 protected:
  // ::testing::Test:
  void SetUp() override;
  void TearDown() override;

  mx::socket StreamDataToSocket(std::string data);

  LedgerRepositoryFactory* ledger_repository_factory() {
    return default_instance_->ledger_repository_factory();
  }

  Ledger* ledger() { return default_instance_->ledger(); }

  LedgerPtr GetTestLedger() { return default_instance_->GetTestLedger(); }

  PagePtr GetTestPage() { return default_instance_->GetTestPage(); }

  PagePtr GetPage(const fidl::Array<uint8_t>& page_id, Status expected_status) {
    return default_instance_->GetPage(page_id, expected_status);
  }

  void DeletePage(const fidl::Array<uint8_t>& page_id, Status expected_status) {
    default_instance_->DeletePage(page_id, expected_status);
  }

  std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance();

 private:
  std::thread socket_thread_;
  ftl::RefPtr<ftl::TaskRunner> socket_task_runner_;
  std::unique_ptr<LedgerAppInstance> default_instance_;

  FTL_DISALLOW_COPY_AND_ASSIGN(IntegrationTest);
};

}  // namespace integration_tests
}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_INTEGRATION_TESTS_INTEGRATION_TEST_H_
