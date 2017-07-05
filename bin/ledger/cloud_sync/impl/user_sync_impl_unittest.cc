// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/user_sync_impl.h"

#include "apps/ledger/src/backoff/backoff.h"
#include "apps/ledger/src/backoff/test/test_backoff.h"
#include "apps/ledger/src/cloud_sync/public/local_version_checker.h"
#include "apps/ledger/src/cloud_sync/test/test_auth_provider.h"
#include "apps/ledger/src/network/fake_network_service.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "lib/ftl/macros.h"

namespace cloud_sync {

namespace {

class TestSyncStateWatcher : public SyncStateWatcher {
 public:
  TestSyncStateWatcher() {}
  ~TestSyncStateWatcher() override{};

  void Notify(SyncStateContainer sync_state) override {}
};

class TestLocalVersionChecker : public LocalVersionChecker {
 public:
  TestLocalVersionChecker(ftl::RefPtr<ftl::TaskRunner> task_runner)
      : task_runner_(task_runner) {}
  ~TestLocalVersionChecker() override {}

  void CheckCloudVersion(std::string auth_token,
                         firebase::Firebase* user_firebase,
                         std::string local_version_path,
                         std::function<void(Status)> callback) override {
    task_runner_->PostTask([ this, callback = std::move(callback) ] {
      callback(status_to_return);
    });
  }

  LocalVersionChecker::Status status_to_return =
      LocalVersionChecker::Status::OK;

 private:
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
};

class UserSyncImplTest : public ::test::TestWithMessageLoop {
 public:
  UserSyncImplTest()
      : network_service_(message_loop_.task_runner()),
        environment_(message_loop_.task_runner(), &network_service_),
        auth_provider_(message_loop_.task_runner()) {
    UserConfig user_config;
    user_config.use_sync = true;
    user_config.server_id = "server-id";
    user_config.user_id = "user-id";
    user_config.user_directory = "/bla";
    user_config.auth_provider = &auth_provider_;
    user_config.local_version_checker =
        std::make_unique<TestLocalVersionChecker>(message_loop_.task_runner());

    local_version_checker_ = static_cast<TestLocalVersionChecker*>(
        user_config.local_version_checker.get());

    user_sync_ = std::make_unique<UserSyncImpl>(
        &environment_, std::move(user_config),
        std::make_unique<backoff::test::TestBackoff>(), &sync_state_watcher_,
        [this] {
          on_version_mismatch_calls_++;
          message_loop_.PostQuitTask();
        });
  }
  ~UserSyncImplTest() override {}

 protected:
  ledger::FakeNetworkService network_service_;
  ledger::Environment environment_;
  test::TestAuthProvider auth_provider_;
  std::unique_ptr<UserSyncImpl> user_sync_;
  TestSyncStateWatcher sync_state_watcher_;
  TestLocalVersionChecker* local_version_checker_;

  int on_version_mismatch_calls_ = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(UserSyncImplTest);
};

// Verifies that the mismatch callback is called if the version checker reports
// that cloud had been erased.
TEST_F(UserSyncImplTest, CloudCheckIncompatible) {
  local_version_checker_->status_to_return =
      LocalVersionChecker::Status::INCOMPATIBLE;
  EXPECT_EQ(0, on_version_mismatch_calls_);
  user_sync_->Start();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, on_version_mismatch_calls_);
}

// Verifies that if the version checker reports that cloud is compatible, upload
// is enabled in LedgerSync.
TEST_F(UserSyncImplTest, CloudCheckOk) {
  local_version_checker_->status_to_return = LocalVersionChecker::Status::OK;
  EXPECT_EQ(0, on_version_mismatch_calls_);
  user_sync_->Start();
  auto ledger_a = user_sync_->CreateLedgerSync("app-id");
  auto ledger_a_ptr = static_cast<LedgerSyncImpl*>(ledger_a.get());
  EXPECT_FALSE(ledger_a_ptr->IsUploadEnabled());
  EXPECT_TRUE(
      RunLoopUntil([ledger_a_ptr] { return ledger_a_ptr->IsUploadEnabled(); }));
  EXPECT_EQ(0, on_version_mismatch_calls_);

  // Verify that newly created LedgerSyncs also have the upload enabled.
  auto ledger_b = user_sync_->CreateLedgerSync("app-id");
  auto ledger_b_ptr = static_cast<LedgerSyncImpl*>(ledger_b.get());
  EXPECT_TRUE(ledger_b_ptr->IsUploadEnabled());
}

}  // namespace

}  // namespace cloud_sync
