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
  IntegrationTest() {}
  virtual ~IntegrationTest() override {}

 protected:
  // ::testing::Test:
  void SetUp() override;
  void TearDown() override;

  mx::socket StreamDataToSocket(std::string data);

  LedgerPtr GetTestLedger();
  PagePtr GetTestPage();
  PagePtr GetPage(const fidl::Array<uint8_t>& page_id, Status expected_status);
  void DeletePage(const fidl::Array<uint8_t>& page_id, Status expected_status);

  LedgerRepositoryFactoryPtr ledger_repository_factory_;
  LedgerPtr ledger_;

 private:
  class LedgerRepositoryFactoryContainer
      : public LedgerRepositoryFactoryImpl::Delegate {
   public:
    LedgerRepositoryFactoryContainer(
        ftl::RefPtr<ftl::TaskRunner> task_runner,
        const std::string& path,
        fidl::InterfaceRequest<LedgerRepositoryFactory> request)
        : network_service_(task_runner),
          environment_(task_runner, &network_service_),
          factory_impl_(this,
                        &environment_,
                        LedgerRepositoryFactoryImpl::ConfigPersistence::FORGET),
          factory_binding_(&factory_impl_, std::move(request)) {}
    ~LedgerRepositoryFactoryContainer() {}

   private:
    // LedgerRepositoryFactoryImpl::Delegate:
    void EraseRepository(
        EraseRemoteRepositoryOperation erase_remote_repository_operation,
        std::function<void(bool)> callback) override {
      FTL_NOTIMPLEMENTED();
      callback(false);
    }

    FakeNetworkService network_service_;
    Environment environment_;
    LedgerRepositoryFactoryImpl factory_impl_;
    fidl::Binding<LedgerRepositoryFactory> factory_binding_;
  };

  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<LedgerRepositoryFactoryContainer> factory_container_;
  std::thread thread_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  std::thread socket_thread_;
  ftl::RefPtr<ftl::TaskRunner> socket_task_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(IntegrationTest);
};

}  // namespace integration_tests
}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_INTEGRATION_TESTS_INTEGRATION_TEST_H_
