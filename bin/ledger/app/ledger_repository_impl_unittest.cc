// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_repository_impl.h"

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/component/cpp/object_dir.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"
#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/storage/fake/fake_db_factory.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/bin/ledger/testing/fake_disk_cleanup_manager.h"
#include "peridot/bin/ledger/testing/test_with_environment.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace ledger {
namespace {

void ExpectRequestsMetric(fuchsia::inspect::Object* object,
                          unsigned long expected_value) {
  bool requests_found = false;
  unsigned long extra_requests_found = 0UL;
  unsigned long requests = 777'777;
  for (auto& index : *object->metrics) {
    if (index.key == "requests") {
      if (!requests_found) {
        requests_found = true;
        requests = index.value.uint_value();
      } else {
        extra_requests_found++;
      }
    }
  }
  EXPECT_TRUE(requests_found);
  EXPECT_EQ(expected_value, requests);
  EXPECT_EQ(0UL, extra_requests_found);
}

class LedgerRepositoryImplTest : public TestWithEnvironment {
 public:
  LedgerRepositoryImplTest() {
    auto fake_page_eviction_manager =
        std::make_unique<FakeDiskCleanupManager>();
    disk_cleanup_manager_ = fake_page_eviction_manager.get();
    component::ExposedObject exposed_object = component::ExposedObject("test");
    object_dir_ = exposed_object.object_dir();

    repository_ = std::make_unique<LedgerRepositoryImpl>(
        std::move(exposed_object), DetachedPath(tmpfs_.root_fd()),
        &environment_,
        std::make_unique<storage::fake::FakeDbFactory>(dispatcher()), nullptr,
        nullptr, std::move(fake_page_eviction_manager), disk_cleanup_manager_);
  }

  ~LedgerRepositoryImplTest() override {}

 protected:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  FakeDiskCleanupManager* disk_cleanup_manager_;
  component::ObjectDir object_dir_;
  std::unique_ptr<LedgerRepositoryImpl> repository_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryImplTest);
};

TEST_F(LedgerRepositoryImplTest, ConcurrentCalls) {
  // Make a first call to DiskCleanUp.
  bool callback_called1 = false;
  Status status1;
  repository_->DiskCleanUp(
      callback::Capture(callback::SetWhenCalled(&callback_called1), &status1));

  // Make a second one before the first one has finished.
  bool callback_called2 = false;
  Status status2;
  repository_->DiskCleanUp(
      callback::Capture(callback::SetWhenCalled(&callback_called2), &status2));

  // Make sure both of them start running.
  RunLoopUntilIdle();

  // Both calls must wait for the cleanup manager.
  EXPECT_FALSE(callback_called1);
  EXPECT_FALSE(callback_called2);

  // Call the cleanup manager callback and expect to see an ok status for both
  // pending callbacks.
  disk_cleanup_manager_->cleanup_callback(Status::OK);
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called1);
  EXPECT_TRUE(callback_called2);
  EXPECT_EQ(Status::OK, status1);
  EXPECT_EQ(Status::OK, status2);
}

TEST_F(LedgerRepositoryImplTest, InspectAPIRequestsMetricOnMultipleBindings) {
  bool zeroth_callback_called = false;
  fuchsia::inspect::Object zeroth_read_object;
  object_dir_.object()->ReadData(callback::Capture(
      callback::SetWhenCalled(&zeroth_callback_called), &zeroth_read_object));
  EXPECT_TRUE(zeroth_callback_called);
  ExpectRequestsMetric(&zeroth_read_object, 0UL);

  ledger_internal::LedgerRepositoryPtr first_ledger_repository_ptr;
  repository_->BindRepository(first_ledger_repository_ptr.NewRequest());
  bool first_callback_called = false;
  fuchsia::inspect::Object first_read_object;
  object_dir_.object()->ReadData(callback::Capture(
      callback::SetWhenCalled(&first_callback_called), &first_read_object));
  EXPECT_TRUE(first_callback_called);
  ExpectRequestsMetric(&first_read_object, 1UL);

  ledger_internal::LedgerRepositoryPtr second_ledger_repository_ptr;
  repository_->BindRepository(second_ledger_repository_ptr.NewRequest());
  bool second_callback_called = false;
  fuchsia::inspect::Object second_read_object;
  object_dir_.object()->ReadData(callback::Capture(
      callback::SetWhenCalled(&second_callback_called), &second_read_object));
  EXPECT_TRUE(second_callback_called);
  ExpectRequestsMetric(&second_read_object, 2UL);
}

}  // namespace
}  // namespace ledger
