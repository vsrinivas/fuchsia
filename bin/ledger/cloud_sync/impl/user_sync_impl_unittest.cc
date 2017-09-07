// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/user_sync_impl.h"

#include <utility>

#include "apps/ledger/src/auth_provider/test/test_auth_provider.h"
#include "apps/ledger/src/backoff/backoff.h"
#include "apps/ledger/src/backoff/test/test_backoff.h"
#include "apps/ledger/src/device_set/cloud_device_set.h"
#include "apps/ledger/src/network/fake_network_service.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"

namespace cloud_sync {

namespace {

class TestSyncStateWatcher : public SyncStateWatcher {
 public:
  TestSyncStateWatcher() {}
  ~TestSyncStateWatcher() override{};

  void Notify(SyncStateContainer /*sync_state*/) override {}
};

class TestCloudDeviceSet : public cloud_provider_firebase::CloudDeviceSet {
 public:
  explicit TestCloudDeviceSet(ftl::RefPtr<ftl::TaskRunner> task_runner)
      : task_runner_(std::move(task_runner)) {}
  ~TestCloudDeviceSet() override {}

  void CheckFingerprint(std::string /*auth_token*/,
                        std::string fingerprint,
                        std::function<void(Status)> callback) override {
    checked_fingerprint = fingerprint;
    task_runner_->PostTask([ this, callback = std::move(callback) ] {
      callback(status_to_return);
    });
  }

  void SetFingerprint(std::string /*auth_token*/,
                      std::string fingerprint,
                      std::function<void(Status)> callback) override {
    set_fingerprint = fingerprint;
    task_runner_->PostTask([ this, callback = std::move(callback) ] {
      callback(status_to_return);
    });
  }

  void WatchFingerprint(std::string /*auth_token*/,
                        std::string fingerprint,
                        std::function<void(Status)> callback) override {
    watched_fingerprint = fingerprint;
    watch_callback = callback;
  }

  CloudDeviceSet::Status status_to_return = CloudDeviceSet::Status::OK;

  std::string checked_fingerprint;
  std::string set_fingerprint;
  std::string watched_fingerprint;
  std::function<void(Status)> watch_callback;

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
    user_config.server_id = "server-id";
    user_config.user_id = "user-id";
    user_config.user_directory = tmp_dir.path();
    user_config.auth_provider = &auth_provider_;
    user_config.cloud_device_set =
        std::make_unique<TestCloudDeviceSet>(message_loop_.task_runner());

    cloud_device_set_ =
        static_cast<TestCloudDeviceSet*>(user_config.cloud_device_set.get());

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
  bool SetFingerprintFile(std::string content) {
    std::string fingerprint_path = user_sync_->GetFingerprintPath();
    return files::WriteFile(fingerprint_path, content.data(), content.size());
  }

  files::ScopedTempDir tmp_dir;
  ledger::FakeNetworkService network_service_;
  ledger::Environment environment_;
  auth_provider::test::TestAuthProvider auth_provider_;
  std::unique_ptr<UserSyncImpl> user_sync_;
  TestSyncStateWatcher sync_state_watcher_;
  TestCloudDeviceSet* cloud_device_set_;

  int on_version_mismatch_calls_ = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(UserSyncImplTest);
};

// Verifies that the mismatch callback is called if the fingerprint appears to
// be erased from the cloud.
TEST_F(UserSyncImplTest, CloudCheckErased) {
  ASSERT_TRUE(SetFingerprintFile("some-value"));
  cloud_device_set_->status_to_return =
      cloud_provider_firebase::CloudDeviceSet::Status::ERASED;
  EXPECT_EQ(0, on_version_mismatch_calls_);
  user_sync_->Start();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, on_version_mismatch_calls_);
}

// Verifies that if the version checker reports that cloud is compatible, upload
// is enabled in LedgerSync.
TEST_F(UserSyncImplTest, CloudCheckOk) {
  ASSERT_TRUE(SetFingerprintFile("some-value"));
  cloud_device_set_->status_to_return =
      cloud_provider_firebase::CloudDeviceSet::Status::OK;
  EXPECT_EQ(0, on_version_mismatch_calls_);
  user_sync_->Start();

  auto ledger_a = user_sync_->CreateLedgerSync("app-id");
  auto ledger_a_ptr = static_cast<LedgerSyncImpl*>(ledger_a.get());
  EXPECT_FALSE(ledger_a_ptr->IsUploadEnabled());
  EXPECT_TRUE(
      RunLoopUntil([ledger_a_ptr] { return ledger_a_ptr->IsUploadEnabled(); }));
  EXPECT_EQ(0, on_version_mismatch_calls_);
  EXPECT_EQ("some-value", cloud_device_set_->checked_fingerprint);

  // Verify that newly created LedgerSyncs also have the upload enabled.
  auto ledger_b = user_sync_->CreateLedgerSync("app-id");
  auto ledger_b_ptr = static_cast<LedgerSyncImpl*>(ledger_b.get());
  EXPECT_TRUE(ledger_b_ptr->IsUploadEnabled());
}

// Verifies that if there is no fingerprint file, it is created and set in the
// cloud.
TEST_F(UserSyncImplTest, CloudCheckSet) {
  EXPECT_FALSE(files::IsFile(user_sync_->GetFingerprintPath()));
  cloud_device_set_->status_to_return =
      cloud_provider_firebase::CloudDeviceSet::Status::OK;
  EXPECT_EQ(0, on_version_mismatch_calls_);
  user_sync_->Start();

  auto ledger = user_sync_->CreateLedgerSync("app-id");
  auto ledger_ptr = static_cast<LedgerSyncImpl*>(ledger.get());
  EXPECT_FALSE(ledger_ptr->IsUploadEnabled());
  EXPECT_TRUE(
      RunLoopUntil([ledger_ptr] { return ledger_ptr->IsUploadEnabled(); }));
  EXPECT_EQ(0, on_version_mismatch_calls_);
  EXPECT_FALSE(cloud_device_set_->set_fingerprint.empty());

  // Verify that the fingerprint file was created.
  EXPECT_TRUE(files::IsFile(user_sync_->GetFingerprintPath()));
}

// Verifies that the cloud watcher for the fingerprint is set and triggers the
// mismatch callback if cloud erase is detected.
TEST_F(UserSyncImplTest, WatchErase) {
  ASSERT_TRUE(SetFingerprintFile("some-value"));
  cloud_device_set_->status_to_return =
      cloud_provider_firebase::CloudDeviceSet::Status::OK;
  user_sync_->Start();

  EXPECT_TRUE(RunLoopUntil(
      [this] { return cloud_device_set_->watch_callback != nullptr; }));
  EXPECT_EQ("some-value", cloud_device_set_->watched_fingerprint);
  EXPECT_EQ(0, on_version_mismatch_calls_);

  cloud_device_set_->watch_callback(
      cloud_provider_firebase::CloudDeviceSet::Status::ERASED);
  EXPECT_EQ(1, on_version_mismatch_calls_);
}

// Verifies that setting the cloud watcher for is retried on network errors.
TEST_F(UserSyncImplTest, WatchRetry) {
  ASSERT_TRUE(SetFingerprintFile("some-value"));
  cloud_device_set_->status_to_return =
      cloud_provider_firebase::CloudDeviceSet::Status::OK;
  user_sync_->Start();

  EXPECT_TRUE(RunLoopUntil(
      [this] { return cloud_device_set_->watch_callback != nullptr; }));

  auto copied_callback = cloud_device_set_->watch_callback;
  cloud_device_set_->watch_callback = nullptr;
  copied_callback(
      cloud_provider_firebase::CloudDeviceSet::Status::NETWORK_ERROR);

  EXPECT_TRUE(RunLoopUntil(
      [this] { return cloud_device_set_->watch_callback != nullptr; }));
  EXPECT_EQ(0, on_version_mismatch_calls_);
}

}  // namespace

}  // namespace cloud_sync
