// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "peridot/public/lib/cloud_provider/validation/convert.h"
#include "peridot/public/lib/cloud_provider/validation/validation_test.h"

namespace cloud_provider {
namespace {

class DeviceSetTest : public ValidationTest, public DeviceSetWatcher {
 public:
  DeviceSetTest() {}
  ~DeviceSetTest() override {}

 protected:
  ::testing::AssertionResult GetDeviceSet(DeviceSetPtr* device_set) {
    device_set->reset();
    Status status = Status::INTERNAL_ERROR;

    cloud_provider_->GetDeviceSet(
        device_set->NewRequest(),
        [&status](Status got_status) { status = got_status; });

    if (!cloud_provider_.WaitForIncomingResponse()) {
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
  DeviceSetPtr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));
}

TEST_F(DeviceSetTest, CheckMissingFingerprint) {
  DeviceSetPtr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));

  Status status = Status::INTERNAL_ERROR;
  device_set->CheckFingerprint(
      ToArray("bazinga"),
      [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(device_set.WaitForIncomingResponse());
  EXPECT_EQ(Status::NOT_FOUND, status);
}

TEST_F(DeviceSetTest, SetAndCheckFingerprint) {
  DeviceSetPtr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));

  Status status = Status::INTERNAL_ERROR;
  device_set->SetFingerprint(ToArray("bazinga"), [&status](Status got_status) {
    status = got_status;
  });
  ASSERT_TRUE(device_set.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  device_set->CheckFingerprint(
      ToArray("bazinga"),
      [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(device_set.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
}

TEST_F(DeviceSetTest, WatchMisingFingerprint) {
  DeviceSetPtr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));
  Status status = Status::INTERNAL_ERROR;
  fidl::Binding<DeviceSetWatcher> binding(this);
  DeviceSetWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  device_set->SetWatcher(ToArray("bazinga"), std::move(watcher),
                         [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(device_set.WaitForIncomingResponse());
  EXPECT_EQ(Status::NOT_FOUND, status);
}

TEST_F(DeviceSetTest, SetAndWatchFingerprint) {
  DeviceSetPtr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));

  Status status = Status::INTERNAL_ERROR;
  device_set->SetFingerprint(ToArray("bazinga"), [&status](Status got_status) {
    status = got_status;
  });
  ASSERT_TRUE(device_set.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  fidl::Binding<DeviceSetWatcher> binding(this);
  DeviceSetWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  device_set->SetWatcher(ToArray("bazinga"), std::move(watcher),
                         [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(device_set.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
}

TEST_F(DeviceSetTest, EraseWhileWatching) {
  DeviceSetPtr device_set;
  ASSERT_TRUE(GetDeviceSet(&device_set));

  Status status = Status::INTERNAL_ERROR;
  device_set->SetFingerprint(ToArray("bazinga"), [&status](Status got_status) {
    status = got_status;
  });
  ASSERT_TRUE(device_set.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  fidl::Binding<DeviceSetWatcher> binding(this);
  DeviceSetWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  device_set->SetWatcher(ToArray("bazinga"), std::move(watcher),
                         [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(device_set.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  EXPECT_EQ(0, on_cloud_erased_calls_);
  device_set->Erase([&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(device_set.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  ASSERT_TRUE(binding.WaitForIncomingMethodCall());
  EXPECT_EQ(1, on_cloud_erased_calls_);
}

}  // namespace
}  // namespace cloud_provider
