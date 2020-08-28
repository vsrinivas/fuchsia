// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/instancelifecycle/test/llcpp/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <vector>

#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

namespace {

using driver_integration_test::IsolatedDevmgr;
using llcpp::fuchsia::device::instancelifecycle::test::InstanceDevice;
using llcpp::fuchsia::device::instancelifecycle::test::Lifecycle;
using llcpp::fuchsia::device::instancelifecycle::test::TestDevice;

class InstanceLifecycleTest : public zxtest::Test {
 public:
  ~InstanceLifecycleTest() override = default;
  void SetUp() override {
    IsolatedDevmgr::Args args;
    args.path_prefix = "/pkg/";

    board_test::DeviceEntry dev = {};
    dev.vid = PDEV_VID_TEST;
    dev.pid = PDEV_PID_INSTANCE_LIFECYCLE_TEST;
    dev.did = 0;
    args.device_list.push_back(dev);

    zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr_);
    ASSERT_OK(status);
    fbl::unique_fd fd;
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), "sys/platform/11:12:0/instance-test", &fd));
    ASSERT_GT(fd.get(), 0);
    ASSERT_OK(fdio_get_service_handle(fd.release(), chan_.reset_and_get_address()));
    ASSERT_NE(chan_.get(), ZX_HANDLE_INVALID);
  }

 protected:
  enum class Event { Open, Close, Unbind, Release };

  // Remove the parent, and then close the instance
  void VerifyPostOpenLifecycleViaRemove(const zx::channel& lifecycle_chan,
                                        zx::channel instance_client);

  // Close the instance
  void VerifyPostOpenLifecycleViaClose(const zx::channel& lifecycle_chan,
                                       zx::channel instance_client);

  static void WaitForEvent(const zx::channel& channel, Event event);
  static bool AreEventsPending(const zx::channel& channel) {
    return channel.wait_one(ZX_CHANNEL_READABLE, zx::time{}, nullptr) == ZX_OK;
  }

  zx::channel chan_;
  IsolatedDevmgr devmgr_;
};

void InstanceLifecycleTest::WaitForEvent(const zx::channel& channel, Event event) {
  Lifecycle::EventHandlers event_handlers;
  event_handlers.on_open = [&]() -> zx_status_t {
    if (event == Event::Open) {
      return ZX_OK;
    }
    return ZX_ERR_BAD_STATE;
  };
  event_handlers.on_close = [&]() -> zx_status_t {
    if (event == Event::Close) {
      return ZX_OK;
    }
    return ZX_ERR_BAD_STATE;
  };
  event_handlers.on_unbind = [&]() -> zx_status_t {
    if (event == Event::Unbind) {
      return ZX_OK;
    }
    return ZX_ERR_BAD_STATE;
  };
  event_handlers.on_release = [&]() -> zx_status_t {
    if (event == Event::Release) {
      return ZX_OK;
    }
    return ZX_ERR_BAD_STATE;
  };
  ASSERT_OK(Lifecycle::Call::HandleEvents(zx::unowned_channel(channel), event_handlers).status());
}

void InstanceLifecycleTest::VerifyPostOpenLifecycleViaRemove(const zx::channel& lifecycle_chan,
                                                             zx::channel instance_client) {
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(lifecycle_chan, Event::Open));

  zx::channel instance_lifecycle_chan, remote;
  {
    ASSERT_OK(zx::channel::create(0, &instance_lifecycle_chan, &remote));
    auto result =
        InstanceDevice::Call::SubscribeToLifecycle(zx::unowned(instance_client), std::move(remote));
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());
  }

  // There shouldn't be anymore pending events yet
  ASSERT_FALSE(AreEventsPending(lifecycle_chan));
  ASSERT_FALSE(AreEventsPending(instance_lifecycle_chan));

  // Request the device begin removal
  {
    auto result = InstanceDevice::Call::RemoveDevice(zx::unowned(instance_client));
    ASSERT_OK(result.status());
  }

  // We should see unbind, followed by close, then release.
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(lifecycle_chan, Event::Unbind));
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(instance_lifecycle_chan, Event::Close));
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(instance_lifecycle_chan, Event::Release));
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(lifecycle_chan, Event::Release));
}

void InstanceLifecycleTest::VerifyPostOpenLifecycleViaClose(const zx::channel& lifecycle_chan,
                                                            zx::channel instance_client) {
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(lifecycle_chan, Event::Open));

  zx::channel instance_lifecycle_chan, remote;
  {
    ASSERT_OK(zx::channel::create(0, &instance_lifecycle_chan, &remote));
    auto result =
        InstanceDevice::Call::SubscribeToLifecycle(zx::unowned(instance_client), std::move(remote));
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());
  }

  // There shouldn't be anymore pending events yet
  ASSERT_FALSE(AreEventsPending(lifecycle_chan));
  ASSERT_FALSE(AreEventsPending(instance_lifecycle_chan));

  // Close the connection to the instance.
  instance_client.reset();
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(instance_lifecycle_chan, Event::Close));
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(instance_lifecycle_chan, Event::Release));
  ASSERT_FALSE(AreEventsPending(lifecycle_chan));
}

// Test the lifecycle of an instance device that's obtained via fuchsia.io/Open
TEST_F(InstanceLifecycleTest, NonPipelinedClientClose) {
  // Subscribe to the device lifecycle events.
  zx::channel lifecycle_chan, remote;
  ASSERT_OK(zx::channel::create(0, &lifecycle_chan, &remote));

  auto result =
      TestDevice::Call::CreateDevice(zx::unowned(chan_), std::move(remote), zx::channel{});
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  // There shouldn't be any pending events yet
  ASSERT_FALSE(AreEventsPending(lifecycle_chan));

  zx::channel instance_client;
  {
    fbl::unique_fd fd;
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), "sys/platform/11:12:0/instance-test/child", &fd));
    ASSERT_GT(fd.get(), 0);
    ASSERT_OK(fdio_get_service_handle(fd.release(), instance_client.reset_and_get_address()));
  }

  ASSERT_NO_FATAL_FAILURES(
      VerifyPostOpenLifecycleViaClose(lifecycle_chan, std::move(instance_client)));
}

// Test the lifecycle of an instance device that's obtained via device_add
TEST_F(InstanceLifecycleTest, PipelinedClientClose) {
  // Subscribe to the device lifecycle events.
  zx::channel lifecycle_chan, lifecycle_remote;
  ASSERT_OK(zx::channel::create(0, &lifecycle_chan, &lifecycle_remote));

  zx::channel instance_client, instance_client_remote;
  ASSERT_OK(zx::channel::create(0, &instance_client, &instance_client_remote));

  auto result = TestDevice::Call::CreateDevice(zx::unowned(chan_), std::move(lifecycle_remote),
                                               std::move(instance_client_remote));
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  ASSERT_NO_FATAL_FAILURES(
      VerifyPostOpenLifecycleViaClose(lifecycle_chan, std::move(instance_client)));
}

// Test the lifecycle of an instance device that's obtained via fuchsia.io/Open
TEST_F(InstanceLifecycleTest, NonPipelinedClientRemoveAndClose) {
  // Subscribe to the device lifecycle events.
  zx::channel lifecycle_chan, remote;
  ASSERT_OK(zx::channel::create(0, &lifecycle_chan, &remote));

  auto result =
      TestDevice::Call::CreateDevice(zx::unowned(chan_), std::move(remote), zx::channel{});
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  // There shouldn't be any pending events yet
  ASSERT_FALSE(AreEventsPending(lifecycle_chan));

  zx::channel instance_client;
  {
    fbl::unique_fd fd;
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), "sys/platform/11:12:0/instance-test/child", &fd));
    ASSERT_GT(fd.get(), 0);
    ASSERT_OK(fdio_get_service_handle(fd.release(), instance_client.reset_and_get_address()));
  }

  ASSERT_NO_FATAL_FAILURES(
      VerifyPostOpenLifecycleViaRemove(lifecycle_chan, std::move(instance_client)));
}

// Test the lifecycle of an instance device that's obtained via device_add
TEST_F(InstanceLifecycleTest, PipelinedClientRemoveAndClose) {
  // Subscribe to the device lifecycle events.
  zx::channel lifecycle_chan, lifecycle_remote;
  ASSERT_OK(zx::channel::create(0, &lifecycle_chan, &lifecycle_remote));

  zx::channel instance_client, instance_client_remote;
  ASSERT_OK(zx::channel::create(0, &instance_client, &instance_client_remote));

  auto result = TestDevice::Call::CreateDevice(zx::unowned(chan_), std::move(lifecycle_remote),
                                               std::move(instance_client_remote));
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  ASSERT_NO_FATAL_FAILURES(
      VerifyPostOpenLifecycleViaRemove(lifecycle_chan, std::move(instance_client)));
}

}  // namespace
