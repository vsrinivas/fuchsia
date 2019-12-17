// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_repository_factory_impl.h"

#include <lib/async/cpp/task.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/platform/detached_path.h"
#include "src/ledger/bin/platform/fd.h"
#include "src/ledger/bin/platform/scoped_tmp_location.h"
#include "src/ledger/bin/platform/unique_fd.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/callback/set_when_called.h"
#include "src/ledger/lib/convert/convert.h"

namespace ledger {
namespace {

constexpr char kUserID[] = "test user ID";

class LedgerRepositoryFactoryImplTest : public TestWithEnvironment {
 public:
  LedgerRepositoryFactoryImplTest() {
    tmp_location_ = environment_.file_system()->CreateScopedTmpLocation();
    repository_factory_ = std::make_unique<LedgerRepositoryFactoryImpl>(&environment_, nullptr);
  }

  LedgerRepositoryFactoryImplTest(const LedgerRepositoryFactoryImplTest&) = delete;
  LedgerRepositoryFactoryImplTest& operator=(const LedgerRepositoryFactoryImplTest&) = delete;
  ~LedgerRepositoryFactoryImplTest() override = default;

  void TearDown() override {
    // Closing the factory, as the destruction must happen on the loop.
    CloseFactory(std::move(repository_factory_));
  }

 protected:
  ::testing::AssertionResult CreateDirectory(const std::string& name);
  ::testing::AssertionResult CallGetRepository(
      const std::string& name, ledger_internal::LedgerRepositoryPtr* ledger_repository_ptr);

  // Helper function to call the |Close| method on a
  // |LedgerRepositoryFactoryImpl|. This is needed as the |Close| method must be
  // called while the loop is running.
  void CloseFactory(std::unique_ptr<LedgerRepositoryFactoryImpl> factory);

  std::unique_ptr<ScopedTmpLocation> tmp_location_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> repository_factory_;
};

::testing::AssertionResult LedgerRepositoryFactoryImplTest::CreateDirectory(
    const std::string& name) {
  if (!environment_.file_system()->CreateDirectory(
          DetachedPath(tmp_location_->path().root_fd(), name))) {
    return ::testing::AssertionFailure() << "Failed to create directory \"" << name << "\"!";
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult LedgerRepositoryFactoryImplTest::CallGetRepository(
    const std::string& name, ledger_internal::LedgerRepositoryPtr* ledger_repository_ptr) {
  unique_fd fd(openat(tmp_location_->path().root_fd(), name.c_str(), O_RDONLY));
  if (!fd.is_valid()) {
    return ::testing::AssertionFailure() << "Failed to validate directory \"" << name << "\"!";
  }

  bool callback_called;
  Status status;

  repository_factory_->GetRepository(CloneChannelFromFileDescriptor(fd.get()), nullptr, kUserID,
                                     ledger_repository_ptr->NewRequest(),
                                     Capture(SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();

  if (!callback_called) {
    return ::testing::AssertionFailure() << "Callback passed to GetRepository not called!";
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "Status of GetRepository call was " << static_cast<int32_t>(status) << "!";
  }
  return ::testing::AssertionSuccess();
}

void LedgerRepositoryFactoryImplTest::CloseFactory(
    std::unique_ptr<LedgerRepositoryFactoryImpl> factory) {
  async::PostTask(dispatcher(), [factory = std::move(factory)]() mutable { factory.reset(); });
  RunLoopUntilIdle();
}

// Verifies that closing a ledger repository closes the LedgerRepository
// connections once all Ledger connections are themselves closed. This test is
// reproduced here due to the interaction between |LedgerRepositoryImpl| and
// |LedgerRepositoryFactoryImpl|.
TEST_F(LedgerRepositoryFactoryImplTest, CloseLedgerRepository) {
  std::string repository_directory = "directory";
  ASSERT_TRUE(CreateDirectory(repository_directory));

  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr1;
  ASSERT_TRUE(CallGetRepository(repository_directory, &ledger_repository_ptr1));

  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr2;
  ASSERT_TRUE(CallGetRepository(repository_directory, &ledger_repository_ptr2));

  LedgerPtr ledger_ptr;

  bool ptr1_closed;
  zx_status_t ptr1_closed_status;
  ledger_repository_ptr1.set_error_handler(
      Capture(SetWhenCalled(&ptr1_closed), &ptr1_closed_status));
  bool ptr2_closed;
  zx_status_t ptr2_closed_status;
  ledger_repository_ptr2.set_error_handler(
      Capture(SetWhenCalled(&ptr2_closed), &ptr2_closed_status));
  bool ledger_closed;
  zx_status_t ledger_closed_status;
  ledger_ptr.set_error_handler(Capture(SetWhenCalled(&ledger_closed), &ledger_closed_status));

  ledger_repository_ptr1->GetLedger(convert::ToArray("ledger"), ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  EXPECT_FALSE(ptr1_closed);
  EXPECT_FALSE(ptr2_closed);
  EXPECT_FALSE(ledger_closed);

  ledger_repository_ptr2->Close();
  RunLoopUntilIdle();
  EXPECT_FALSE(ptr1_closed);
  EXPECT_FALSE(ptr2_closed);
  EXPECT_FALSE(ledger_closed);

  ledger_ptr.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(ptr1_closed);
  EXPECT_TRUE(ptr2_closed);

  EXPECT_EQ(ptr1_closed_status, ZX_OK);
  EXPECT_EQ(ptr2_closed_status, ZX_OK);
}

// Verifies that closing LedgerRepositoryFactoryImpl closes all the elements under it.
TEST_F(LedgerRepositoryFactoryImplTest, CloseFactory) {
  auto repository_factory = std::make_unique<LedgerRepositoryFactoryImpl>(&environment_, nullptr);

  std::unique_ptr<ScopedTmpLocation> tmp_location =
      environment_.file_system()->CreateScopedTmpLocation();
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;

  bool get_repository_called;
  Status status;

  repository_factory->GetRepository(CloneChannelFromFileDescriptor(tmp_location->path().root_fd()),
                                    nullptr, "", ledger_repository_ptr.NewRequest(),
                                    Capture(SetWhenCalled(&get_repository_called), &status));

  bool channel_closed;
  zx_status_t zx_status;

  ledger_repository_ptr.set_error_handler(Capture(SetWhenCalled(&channel_closed), &zx_status));

  RunLoopUntilIdle();

  EXPECT_TRUE(get_repository_called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(channel_closed);

  CloseFactory(std::move(repository_factory));

  EXPECT_TRUE(channel_closed);
}

}  // namespace
}  // namespace ledger
