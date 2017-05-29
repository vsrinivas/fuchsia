// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_INTEGRATION_TESTS_TEST_UTILS_H_
#define APPS_LEDGER_SRC_APP_INTEGRATION_TESTS_TEST_UTILS_H_

#include <vector>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/app/ledger_repository_factory_impl.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"
#include "mx/vmo.h"

namespace ledger {
namespace integration_tests {

fidl::Array<uint8_t> RandomArray(size_t size,
                                 const std::vector<uint8_t>& prefix);

fidl::Array<uint8_t> RandomArray(int size);

fidl::Array<uint8_t> PageGetId(PagePtr* page);

PageSnapshotPtr PageGetSnapshot(PagePtr* page,
                                fidl::Array<uint8_t> prefix = nullptr);

fidl::Array<fidl::Array<uint8_t>> SnapshotGetKeys(PageSnapshotPtr* snapshot,
                                                  fidl::Array<uint8_t> start);
fidl::Array<fidl::Array<uint8_t>> SnapshotGetKeys(PageSnapshotPtr* snapshot,
                                                  fidl::Array<uint8_t> start,
                                                  int* num_queries);

fidl::Array<EntryPtr> SnapshotGetEntries(PageSnapshotPtr* snapshot,
                                         fidl::Array<uint8_t> start);
fidl::Array<EntryPtr> SnapshotGetEntries(PageSnapshotPtr* snapshot,
                                         fidl::Array<uint8_t> start,
                                         int* num_queries);

std::string SnapshotFetchPartial(PageSnapshotPtr* snapshot,
                                 fidl::Array<uint8_t> key,
                                 int64_t offset,
                                 int64_t max_size);

std::string ToString(const mx::vmo& vmo);

fidl::Array<uint8_t> ToArray(const mx::vmo& vmo);

class LedgerRepositoryFactoryContainer {
 public:
  LedgerRepositoryFactoryContainer(
      ftl::RefPtr<ftl::TaskRunner> task_runner,
      const std::string& path,
      fidl::InterfaceRequest<LedgerRepositoryFactory> request)
      : environment_(task_runner, nullptr),
        factory_impl_(&environment_,
                      LedgerRepositoryFactoryImpl::ConfigPersistence::FORGET),
        factory_binding_(&factory_impl_, std::move(request)) {}
  ~LedgerRepositoryFactoryContainer() {}

 private:
  Environment environment_;
  LedgerRepositoryFactoryImpl factory_impl_;
  fidl::Binding<LedgerRepositoryFactory> factory_binding_;
};

class LedgerApplicationBaseTest : public test::TestWithMessageLoop {
 public:
  LedgerApplicationBaseTest() {}
  virtual ~LedgerApplicationBaseTest() override {}

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
  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<LedgerRepositoryFactoryContainer> factory_container_;
  std::thread thread_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  std::thread socket_thread_;
  ftl::RefPtr<ftl::TaskRunner> socket_task_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerApplicationBaseTest);
};

}  // namespace integration_tests
}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_INTEGRATION_TESTS_TEST_UTILS_H_
