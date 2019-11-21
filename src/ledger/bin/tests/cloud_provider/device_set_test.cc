// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/cloud/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/ledger/bin/tests/cloud_provider/types.h"
#include "src/ledger/bin/tests/cloud_provider/validation_test.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/logging.h"

namespace cloud_provider {
namespace {

class DeviceSetTest : public ValidationTest, public DeviceSetWatcher {
 public:
  DeviceSetTest() = default;
  ~DeviceSetTest() override = default;

 protected:
  ::testing::AssertionResult GetDeviceSet(DeviceSetSyncPtr* device_set) {
    *device_set = DeviceSetSyncPtr();
    Status status = Status::INTERNAL_ERROR;

    if (cloud_provider_->GetDeviceSet(device_set->NewRequest(), &status) != ZX_OK) {
      return ::testing::AssertionFailure()
             << "Failed to retrieve the device set due to channel error.";
    }

    if (status != Status::OK) {
      return ::testing::AssertionFailure() << "Failed to retrieve the device set, received status: "
                                           << fidl::ToUnderlying(status);
    }

    return ::testing::AssertionSuccess();
  }

  int on_cloud_erased_calls_ = 0;

 private:
  // DeviceSetWatcher:
  void OnCloudErased() override { on_cloud_erased_calls_++; }

  void OnError(Status status) override {
    // Do nothing - the validation test suite currently does not inject and test
    // for network errors.
    FXL_NOTIMPLEMENTED();
  }
};

TEST_F(DeviceSetTest, GetDeviceSet) {
  DeviceSetSyncPtr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));
}

TEST_F(DeviceSetTest, CheckMissingFingerprint) {
  const std::vector<uint8_t> fingerprint = GetUniqueRandomId();

  DeviceSetSyncPtr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));

  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(device_set->CheckFingerprint(fingerprint, &status), ZX_OK);
  EXPECT_EQ(status, Status::NOT_FOUND);
}

TEST_F(DeviceSetTest, SetAndCheckFingerprint) {
  const std::vector<uint8_t> fingerprint = GetUniqueRandomId();

  DeviceSetSyncPtr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));

  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(device_set->SetFingerprint(fingerprint, &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  ASSERT_EQ(device_set->CheckFingerprint(fingerprint, &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);
}

TEST_F(DeviceSetTest, WatchMisingFingerprint) {
  const std::vector<uint8_t> fingerprint = GetUniqueRandomId();

  DeviceSetSyncPtr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));
  Status status = Status::INTERNAL_ERROR;
  fidl::Binding<DeviceSetWatcher> binding(this);
  DeviceSetWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(device_set->SetWatcher(fingerprint, std::move(watcher), &status), ZX_OK);
  EXPECT_EQ(status, Status::NOT_FOUND);
}

TEST_F(DeviceSetTest, SetAndWatchFingerprint) {
  const std::vector<uint8_t> fingerprint = GetUniqueRandomId();

  DeviceSetSyncPtr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));

  Status status = Status::INTERNAL_ERROR;
  EXPECT_EQ(device_set->SetFingerprint(fingerprint, &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  fidl::Binding<DeviceSetWatcher> binding(this);
  DeviceSetWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(device_set->SetWatcher(fingerprint, std::move(watcher), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);
}

TEST_F(DeviceSetTest, EraseWhileWatching) {
  const std::vector<uint8_t> fingerprint = GetUniqueRandomId();

  DeviceSetSyncPtr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));

  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(device_set->SetFingerprint(fingerprint, &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  fidl::Binding<DeviceSetWatcher> binding(this);
  DeviceSetWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(device_set->SetWatcher(fingerprint, std::move(watcher), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  EXPECT_EQ(on_cloud_erased_calls_, 0);
  ASSERT_EQ(device_set->Erase(&status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  ASSERT_EQ(binding.WaitForMessage(), ZX_OK);
  EXPECT_EQ(on_cloud_erased_calls_, 1);
}

}  // namespace
}  // namespace cloud_provider
