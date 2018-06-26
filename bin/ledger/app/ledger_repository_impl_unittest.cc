// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_repository_impl.h"

#include "gtest/gtest.h"
#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/gtest/test_with_message_loop.h"
#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace ledger {
namespace {

class FakePageEvictionManager : public PageEvictionManager {
 public:
  FakePageEvictionManager() {}
  virtual ~FakePageEvictionManager() override {}

  void OnPageOpened(fxl::StringView ledger_name,
                    storage::PageIdView page_id) override {}

  void OnPageClosed(fxl::StringView ledger_name,
                    storage::PageIdView page_id) override {}

  void TryCleanUp(std::function<void(Status)> callback) override {
    // Do not call the callback directly.
    cleanup_callback = std::move(callback);
  }

  std::function<void(Status)> cleanup_callback;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(FakePageEvictionManager);
};

class LedgerRepositoryImplTest : public gtest::TestLoopFixture {
 public:
  LedgerRepositoryImplTest()
      : environment_(EnvironmentBuilder().SetAsync(dispatcher()).Build()) {
    auto fake_page_eviction_manager =
        std::make_unique<FakePageEvictionManager>();
    page_eviction_manager_ = fake_page_eviction_manager.get();

    repository_ = std::make_unique<LedgerRepositoryImpl>(
        DetachedPath(tmpfs_.root_fd()), &environment_, nullptr, nullptr,
        std::move(fake_page_eviction_manager));
  }

  ~LedgerRepositoryImplTest() override {}

 protected:
  ScopedTmpFS tmpfs_;
  Environment environment_;
  std::unique_ptr<LedgerRepositoryImpl> repository_;
  FakePageEvictionManager* page_eviction_manager_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryImplTest);
};  // namespace

TEST_F(LedgerRepositoryImplTest, DiskCleanUpError) {
  // Make a first call to DiskCleanUp.
  bool callback_called1 = false;
  Status status1;
  repository_->DiskCleanUp(
      callback::Capture(callback::SetWhenCalled(&callback_called1), &status1));

  // Make a second one before the first one has finished.
  bool callback_called2 = false;
  ledger::Status status2;
  repository_->DiskCleanUp(
      callback::Capture(callback::SetWhenCalled(&callback_called2), &status2));

  // Make sure both of them start running.
  RunLoopUntilIdle();

  // Only the second one should terminate with ILLEGAL_STATE status.
  EXPECT_FALSE(callback_called1);
  EXPECT_TRUE(callback_called2);
  EXPECT_EQ(Status::ILLEGAL_STATE, status2);

  // Call the callback and expect to see an ok status for the first one.
  page_eviction_manager_->cleanup_callback(Status::OK);
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called1);
  EXPECT_EQ(Status::OK, status1);
}

}  // namespace
}  // namespace ledger
