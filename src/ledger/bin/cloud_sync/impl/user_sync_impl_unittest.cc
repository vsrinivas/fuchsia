// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/user_sync_impl.h"

#include <lib/gtest/test_loop_fixture.h>
#include <unistd.h>

#include <utility>

#include "src/ledger/bin/clocks/public/device_fingerprint_manager.h"
#include "src/ledger/bin/clocks/public/types.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_cloud_provider.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/platform/scoped_tmp_location.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/backoff/testing/test_backoff.h"

namespace cloud_sync {

namespace {

class TestSyncStateWatcher : public SyncStateWatcher {
 public:
  TestSyncStateWatcher() = default;
  ~TestSyncStateWatcher() override = default;

  void Notify(SyncStateContainer /*sync_state*/) override {}
};

class TestDeviceFingerprintManager : public clocks::DeviceFingerprintManager {
 public:
  TestDeviceFingerprintManager() = default;
  ~TestDeviceFingerprintManager() override = default;

  ledger::Status GetDeviceFingerprint(coroutine::CoroutineHandler* /*handler*/,
                                      clocks::DeviceFingerprint* device_fingerprint,
                                      CloudUploadStatus* status) override {
    *device_fingerprint = fingerprint_;
    *status = sync_status_ ? CloudUploadStatus::UPLOADED : CloudUploadStatus::NOT_UPLOADED;
    return ledger::Status::OK;
  }

  // Records that a device fingerprint has been synced with the cloud.
  ledger::Status SetDeviceFingerprintSynced(coroutine::CoroutineHandler* /*handler*/) override {
    sync_status_ = true;
    return ledger::Status::OK;
  }

  clocks::DeviceFingerprint fingerprint_ = "unsynced_fingerprint";
  bool sync_status_ = false;
};

class UserSyncImplTest : public ledger::TestWithEnvironment {
 public:
  UserSyncImplTest()
      : tmp_location_(environment_.file_system()->CreateScopedTmpLocation()),
        cloud_provider_(cloud_provider_ptr_.NewRequest()),
        encryption_service_(dispatcher()) {
    UserConfig user_config;
    user_config.user_directory = tmp_location_->path();
    user_config.cloud_provider = std::move(cloud_provider_ptr_);

    auto backoff = std::make_unique<ledger::TestBackoff>();
    backoff->SetOnGetNext([this] {
      // Make RunLoopUntilIdle() return once a backoff is requested, to avoid an
      // infinite loop.
      QuitLoop();
    });

    user_sync_ = std::make_unique<UserSyncImpl>(
        &environment_, std::move(user_config), std::move(backoff),
        [this] { on_version_mismatch_calls_++; }, &fingerprint_manager_);
    user_sync_->SetSyncWatcher(&sync_state_watcher_);
  }
  UserSyncImplTest(const UserSyncImplTest&) = delete;
  UserSyncImplTest& operator=(const UserSyncImplTest&) = delete;
  ~UserSyncImplTest() override = default;

 protected:
  void SetFingerprintFile(std::string content) {
    fingerprint_manager_.fingerprint_ = std::move(content);
    fingerprint_manager_.sync_status_ = true;
  }

  std::unique_ptr<ledger::ScopedTmpLocation> tmp_location_;
  cloud_provider::CloudProviderPtr cloud_provider_ptr_;
  TestCloudProvider cloud_provider_;
  std::unique_ptr<UserSyncImpl> user_sync_;
  encryption::FakeEncryptionService encryption_service_;
  TestSyncStateWatcher sync_state_watcher_;
  TestDeviceFingerprintManager fingerprint_manager_;

  int on_version_mismatch_calls_ = 0;
};

// Verifies that the mismatch callback is called if the fingerprint appears to
// be erased from the cloud.
TEST_F(UserSyncImplTest, CloudCheckErased) {
  SetFingerprintFile("some-value");
  cloud_provider_.device_set.status_to_return = cloud_provider::Status::NOT_FOUND;
  EXPECT_EQ(on_version_mismatch_calls_, 0);
  user_sync_->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(on_version_mismatch_calls_, 1);
}

// Verifies that if the version checker reports that cloud is compatible, upload
// is enabled in LedgerSync.
TEST_F(UserSyncImplTest, CloudCheckOk) {
  SetFingerprintFile("some-value");
  cloud_provider_.device_set.status_to_return = cloud_provider::Status::OK;
  EXPECT_EQ(on_version_mismatch_calls_, 0);
  user_sync_->Start();

  auto ledger_a = user_sync_->CreateLedgerSync("app-id", &encryption_service_);
  auto ledger_a_ptr = static_cast<LedgerSyncImpl*>(ledger_a.get());
  EXPECT_FALSE(ledger_a_ptr->IsUploadEnabled());
  RunLoopUntilIdle();
  EXPECT_TRUE(ledger_a_ptr->IsUploadEnabled());
  EXPECT_EQ(on_version_mismatch_calls_, 0);
  EXPECT_EQ(cloud_provider_.device_set.checked_fingerprint, "some-value");

  // Verify that newly created LedgerSyncs also have the upload enabled.
  auto ledger_b = user_sync_->CreateLedgerSync("app-id", &encryption_service_);
  auto ledger_b_ptr = static_cast<LedgerSyncImpl*>(ledger_b.get());
  EXPECT_TRUE(ledger_b_ptr->IsUploadEnabled());
}

// Verifies that if there is no fingerprint file, it is created and set in the
// cloud.
TEST_F(UserSyncImplTest, CloudCheckSet) {
  auto fingerprint_path = user_sync_->GetFingerprintPath();
  EXPECT_FALSE(fingerprint_manager_.sync_status_);
  cloud_provider_.device_set.status_to_return = cloud_provider::Status::OK;
  EXPECT_EQ(on_version_mismatch_calls_, 0);
  user_sync_->Start();

  auto ledger = user_sync_->CreateLedgerSync("app-id", &encryption_service_);
  auto ledger_ptr = static_cast<LedgerSyncImpl*>(ledger.get());
  EXPECT_FALSE(ledger_ptr->IsUploadEnabled());
  RunLoopUntilIdle();
  EXPECT_TRUE(ledger_ptr->IsUploadEnabled());
  EXPECT_EQ(on_version_mismatch_calls_, 0);
  EXPECT_FALSE(cloud_provider_.device_set.set_fingerprint.empty());

  // Verify that the fingerprint file was created.
  EXPECT_TRUE(fingerprint_manager_.sync_status_);
}

// Verifies that the cloud watcher for the fingerprint is set and triggers the
// mismatch callback when cloud erase is detected.
TEST_F(UserSyncImplTest, WatchErase) {
  SetFingerprintFile("some-value");
  cloud_provider_.device_set.status_to_return = cloud_provider::Status::OK;
  user_sync_->Start();

  RunLoopUntilIdle();
  EXPECT_TRUE(cloud_provider_.device_set.set_watcher.is_bound());
  EXPECT_EQ(cloud_provider_.device_set.watched_fingerprint, "some-value");
  EXPECT_EQ(on_version_mismatch_calls_, 0);

  cloud_provider_.device_set.set_watcher->OnCloudErased();
  RunLoopUntilIdle();
  EXPECT_EQ(on_version_mismatch_calls_, 1);
}

// Verifies that setting the cloud watcher for is retried on network errors.
TEST_F(UserSyncImplTest, WatchRetry) {
  SetFingerprintFile("some-value");
  cloud_provider_.device_set.set_watcher_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  user_sync_->Start();

  RunLoopUntilIdle();
  EXPECT_EQ(cloud_provider_.device_set.set_watcher_calls, 1);
}

}  // namespace

}  // namespace cloud_sync
