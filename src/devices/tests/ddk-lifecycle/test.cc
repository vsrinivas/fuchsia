// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/lifecycle/test/llcpp/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <vector>

#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;
using llcpp::fuchsia::device::lifecycle::test::Lifecycle;
using llcpp::fuchsia::device::lifecycle::test::TestDevice;

class LifecycleTest : public zxtest::Test {
 public:
  ~LifecycleTest() override = default;
  void SetUp() override {
    IsolatedDevmgr::Args args;
    args.load_drivers.push_back("/boot/driver/ddk-lifecycle-test.so");

    board_test::DeviceEntry dev = {};
    dev.vid = PDEV_VID_TEST;
    dev.pid = PDEV_PID_LIFECYCLE_TEST;
    dev.did = 0;
    args.device_list.push_back(dev);

    zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr_);
    ASSERT_OK(status);
    fbl::unique_fd fd;
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), "sys/platform/11:10:0/ddk-lifecycle-test", &fd));
    ASSERT_GT(fd.get(), 0);
    ASSERT_OK(fdio_get_service_handle(fd.release(), chan_.reset_and_get_address()));
    ASSERT_NE(chan_.get(), ZX_HANDLE_INVALID);

    // Subscribe to the device lifecycle events.
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));

    auto result = TestDevice::Call::SubscribeToLifecycle(zx::unowned(chan_), std::move(remote));
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());
    lifecycle_chan_ = std::move(local);
  }

 protected:
  void WaitPreRelease(uint64_t child_id);

  zx::channel chan_;
  IsolatedDevmgr devmgr_;

  zx::channel lifecycle_chan_;
};

void LifecycleTest::WaitPreRelease(uint64_t child_id) {
  bool removed = false;
  uint64_t device_id = 0;
  while (!removed) {
    Lifecycle::EventHandlers event_handlers;
    event_handlers.on_child_pre_release = [&](uint64_t id) -> zx_status_t {
      device_id = id;
      removed = true;
      return ZX_OK;
    };
    ASSERT_OK(Lifecycle::Call::HandleEvents(zx::unowned_channel(lifecycle_chan_),
                                            std::move(event_handlers)));
  }
  ASSERT_EQ(device_id, child_id);
}

TEST_F(LifecycleTest, ChildPreRelease) {
  // Add some child devices and store the returned ids.
  std::vector<uint64_t> child_ids;
  const uint32_t num_children = 10;
  for (unsigned int i = 0; i < num_children; i++) {
    auto result = TestDevice::Call::AddChild(zx::unowned(chan_), true /* complete_init */,
                                             ZX_OK /* init_status */);
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());
    child_ids.push_back(result->result.response().child_id);
  }

  // Remove the child devices and check the test device received the pre-release notifications.
  for (auto child_id : child_ids) {
    auto result = TestDevice::Call::RemoveChild(zx::unowned(chan_), child_id);
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());

    // Wait for the child pre-release notification.
    ASSERT_NO_FATAL_FAILURES(WaitPreRelease(child_id));
  }
}

TEST_F(LifecycleTest, Init) {
  // Add a child device that does not complete its init hook yet.
  uint64_t child_id;
  auto result = TestDevice::Call::AddChild(zx::unowned(chan_), false /* complete_init */,
                                           ZX_OK /* init_status */);
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());
  child_id = result->result.response().child_id;

  auto remove_result = TestDevice::Call::RemoveChild(zx::unowned(chan_), child_id);
  ASSERT_OK(remove_result.status());
  ASSERT_FALSE(remove_result->result.is_err());

  auto init_result = TestDevice::Call::CompleteChildInit(zx::unowned(chan_), child_id);
  ASSERT_OK(init_result.status());
  ASSERT_FALSE(init_result->result.is_err());

  // Wait for the child pre-release notification.
  ASSERT_NO_FATAL_FAILURES(WaitPreRelease(child_id));
}

// Tests that the child device is removed if init fails.
TEST_F(LifecycleTest, FailedInit) {
  uint64_t child_id;
  auto result = TestDevice::Call::AddChild(zx::unowned(chan_), true /* complete_init */,
                                           ZX_ERR_BAD_STATE /* init_status */);
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());
  child_id = result->result.response().child_id;

  // Wait for the child pre-release notification.
  ASSERT_NO_FATAL_FAILURES(WaitPreRelease(child_id));
}
