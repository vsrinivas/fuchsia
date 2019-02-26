// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_repository_impl.h"

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/component/cpp/expose.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/app/ledger_repository_factory_impl.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/fake_disk_cleanup_manager.h"
#include "src/ledger/bin/testing/inspect.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace ledger {
namespace {

class LedgerRepositoryImplTest : public TestWithEnvironment {
 public:
  LedgerRepositoryImplTest() {
    auto fake_page_eviction_manager =
        std::make_unique<FakeDiskCleanupManager>();
    disk_cleanup_manager_ = fake_page_eviction_manager.get();

    repository_ = std::make_unique<LedgerRepositoryImpl>(
        DetachedPath(tmpfs_.root_fd()), &environment_,
        std::make_unique<storage::fake::FakeDbFactory>(dispatcher()), nullptr,
        nullptr, std::move(fake_page_eviction_manager), disk_cleanup_manager_);
  }

  ~LedgerRepositoryImplTest() override {}

 protected:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  FakeDiskCleanupManager* disk_cleanup_manager_;
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
  // When nothing has bound to the repository, check that the "requests" metric
  // is present and is zero.
  bool zeroth_callback_called = false;
  fuchsia::inspect::Object zeroth_read_object;
  component::Object::ObjectVector zeroth_out;

  repository_->Inspect("zeroth", &zeroth_out);

  ASSERT_EQ(1UL, zeroth_out.size());
  zeroth_out.at(0).get()->ReadData(callback::Capture(
      callback::SetWhenCalled(&zeroth_callback_called), &zeroth_read_object));
  EXPECT_TRUE(zeroth_callback_called);
  ExpectRequestsMetric(&zeroth_read_object, 0UL);

  // When one binding has been made to the repository, check that the "requests"
  // metric is present and is one.
  ledger_internal::LedgerRepositoryPtr first_ledger_repository_ptr;
  bool first_callback_called = false;
  fuchsia::inspect::Object first_read_object;
  component::Object::ObjectVector first_out;
  repository_->BindRepository(first_ledger_repository_ptr.NewRequest());

  repository_->Inspect("first", &first_out);

  ASSERT_EQ(1UL, first_out.size());
  first_out.at(0).get()->ReadData(callback::Capture(
      callback::SetWhenCalled(&first_callback_called), &first_read_object));
  EXPECT_TRUE(first_callback_called);
  ExpectRequestsMetric(&first_read_object, 1UL);

  // When two bindings have been made to the repository, check that the
  // "requests" metric is present and is two.
  ledger_internal::LedgerRepositoryPtr second_ledger_repository_ptr;
  bool second_callback_called = false;
  fuchsia::inspect::Object second_read_object;
  component::Object::ObjectVector second_out;
  repository_->BindRepository(second_ledger_repository_ptr.NewRequest());

  repository_->Inspect("second", &second_out);

  ASSERT_EQ(1UL, second_out.size());
  second_out.at(0).get()->ReadData(callback::Capture(
      callback::SetWhenCalled(&second_callback_called), &second_read_object));
  EXPECT_TRUE(second_callback_called);
  ExpectRequestsMetric(&second_read_object, 2UL);
}

}  // namespace
}  // namespace ledger
