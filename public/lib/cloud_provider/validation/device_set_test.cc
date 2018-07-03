// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/cloud_provider/validation/convert.h>
#include <lib/cloud_provider/validation/types.h>
#include <lib/cloud_provider/validation/validation_test.h>
#include <lib/fxl/logging.h>

#include "gtest/gtest.h"

namespace cloud_provider {
namespace {

class DeviceSetTest : public ValidationTest, public DeviceSetWatcher {
 public:
  DeviceSetTest() {}
  ~DeviceSetTest() override {}

 protected:
  ::testing::AssertionResult GetDeviceSet(DeviceSetSync2Ptr* device_set) {
    *device_set = DeviceSetSync2Ptr();
    Status status = Status::INTERNAL_ERROR;

    if (cloud_provider_->GetDeviceSet(device_set->NewRequest(), &status)
            .statvs != ZX_OK) {
      return ::testing::AssertionFailure()
             << "Failed to retrieve the device set due to channel error.";
    }

    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "Failed to retrieve the device set, received status: "
             << status;
    }

    return ::testing::AssertionSuccess();
  }

  int on_cloud_erased_calls_ = 0;

 private:
  // DeviceSetWatcher:
  void OnCloudErased() override { on_cloud_erased_calls_++; }

  void OnNetworkError() override {
    // Do nothing - the validation test suite currently does not inject and test
    // for network errors.
    FXL_NOTIMPLEMENTED();
  }
};

TEST_F(DeviceSetTest, GetDeviceSet) {
  DeviceSetSync2Ptr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));
}

TEST_F(DeviceSetTest, CheckMissingFingerprint) {
  DeviceSetSync2Ptr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));

  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(ZX_OK,
            device_set->CheckFingerprint(ToArray("bazinga"), &status).statvs);
  EXPECT_EQ(Status::NOT_FOUND, status);
}

TEST_F(DeviceSetTest, SetAndCheckFingerprint) {
  DeviceSetSync2Ptr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));

  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(ZX_OK,
            device_set->SetFingerprint(ToArray("bazinga"), &status).statvs);
  EXPECT_EQ(Status::OK, status);

  ASSERT_EQ(ZX_OK,
            device_set->CheckFingerprint(ToArray("bazinga"), &status).statvs);
  EXPECT_EQ(Status::OK, status);
}

TEST_F(DeviceSetTest, WatchMisingFingerprint) {
  DeviceSetSync2Ptr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));
  Status status = Status::INTERNAL_ERROR;
  fidl::Binding<DeviceSetWatcher> binding(this);
  DeviceSetWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(
      ZX_OK,
      device_set->SetWatcher(ToArray("bazinga"), std::move(watcher), &status)
          .statvs);
  EXPECT_EQ(Status::NOT_FOUND, status);
}

TEST_F(DeviceSetTest, SetAndWatchFingerprint) {
  DeviceSetSync2Ptr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));

  Status status = Status::INTERNAL_ERROR;
  EXPECT_EQ(ZX_OK,
            device_set->SetFingerprint(ToArray("bazinga"), &status).statvs);
  EXPECT_EQ(Status::OK, status);

  fidl::Binding<DeviceSetWatcher> binding(this);
  DeviceSetWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(
      ZX_OK,
      device_set->SetWatcher(ToArray("bazinga"), std::move(watcher), &status)
          .statvs);
  EXPECT_EQ(Status::OK, status);
}

TEST_F(DeviceSetTest, EraseWhileWatching) {
  DeviceSetSync2Ptr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));

  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(ZX_OK,
            device_set->SetFingerprint(ToArray("bazinga"), &status).statvs);
  EXPECT_EQ(Status::OK, status);

  fidl::Binding<DeviceSetWatcher> binding(this);
  DeviceSetWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(
      ZX_OK,
      device_set->SetWatcher(ToArray("bazinga"), std::move(watcher), &status)
          .statvs);
  EXPECT_EQ(Status::OK, status);

  EXPECT_EQ(0, on_cloud_erased_calls_);
  ASSERT_EQ(ZX_OK, device_set->Erase(&status).statvs);
  EXPECT_EQ(Status::OK, status);

  ASSERT_EQ(ZX_OK, binding.WaitForMessage());
  EXPECT_EQ(1, on_cloud_erased_calls_);
}

}  // namespace
}  // namespace cloud_provider
