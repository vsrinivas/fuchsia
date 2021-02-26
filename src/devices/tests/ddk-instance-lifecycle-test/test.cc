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
    ASSERT_OK(fdio_get_service_handle(fd.release(), device_.channel().reset_and_get_address()));
    ASSERT_TRUE(device_.is_valid());
  }

 protected:
  enum class Event { Open, Close, Unbind, Release };

  // Remove the parent, and then close the instance
  void VerifyPostOpenLifecycleViaRemove(fidl::UnownedClientEnd<Lifecycle> lifecycle_chan,
                                        fidl::ClientEnd<InstanceDevice> instance_client);

  // Close the instance
  void VerifyPostOpenLifecycleViaClose(fidl::UnownedClientEnd<Lifecycle> lifecycle_chan,
                                       fidl::ClientEnd<InstanceDevice> instance_client);

  static void WaitForEvent(fidl::UnownedClientEnd<Lifecycle> lifecycle_client, Event event);
  static bool AreEventsPending(fidl::UnownedClientEnd<Lifecycle> lifecycle_client) {
    return lifecycle_client.channel()->wait_one(ZX_CHANNEL_READABLE, zx::time{}, nullptr) == ZX_OK;
  }

  fidl::ClientEnd<TestDevice> device_;
  IsolatedDevmgr devmgr_;
};

void InstanceLifecycleTest::WaitForEvent(fidl::UnownedClientEnd<Lifecycle> lifecycle_client,
                                         Event expected_event) {
  class EventHandler : public Lifecycle::SyncEventHandler {
   public:
    explicit EventHandler(Event expected_event) : expected_event_(expected_event) {}

    bool ok() const { return ok_; }

    void OnOpen(Lifecycle::OnOpenResponse* event) override { ok_ = expected_event_ == Event::Open; }

    void OnClose(Lifecycle::OnCloseResponse* event) override {
      ok_ = expected_event_ == Event::Close;
    };

    void OnUnbind(Lifecycle::OnUnbindResponse* event) override {
      ok_ = expected_event_ == Event::Unbind;
    }

    void OnRelease(Lifecycle::OnReleaseResponse* event) override {
      ok_ = expected_event_ == Event::Release;
    }

    zx_status_t Unknown() override { return ZX_ERR_NOT_SUPPORTED; }

   private:
    const Event expected_event_;
    bool ok_ = false;
  };

  EventHandler event_handler(expected_event);
  ASSERT_OK(event_handler.HandleOneEvent(lifecycle_client).status());
  ASSERT_TRUE(event_handler.ok());
}

void InstanceLifecycleTest::VerifyPostOpenLifecycleViaRemove(
    fidl::UnownedClientEnd<Lifecycle> lifecycle_chan,
    fidl::ClientEnd<InstanceDevice> instance_client) {
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(lifecycle_chan, Event::Open));

  auto endpoints = fidl::CreateEndpoints<Lifecycle>();
  ASSERT_OK(endpoints.status_value());
  auto [instance_lifecycle_chan, remote] = *std::move(endpoints);
  {
    auto result = InstanceDevice::Call::SubscribeToLifecycle(instance_client, std::move(remote));
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());
  }

  // There shouldn't be anymore pending events yet
  ASSERT_FALSE(AreEventsPending(lifecycle_chan));
  ASSERT_FALSE(AreEventsPending(instance_lifecycle_chan));

  // Request the device begin removal
  {
    auto result = InstanceDevice::Call::RemoveDevice(instance_client);
    ASSERT_OK(result.status());
  }

  // We should see unbind, followed by close, then release.
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(lifecycle_chan, Event::Unbind));
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(instance_lifecycle_chan, Event::Close));
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(instance_lifecycle_chan, Event::Release));
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(lifecycle_chan, Event::Release));
}

void InstanceLifecycleTest::VerifyPostOpenLifecycleViaClose(
    fidl::UnownedClientEnd<Lifecycle> lifecycle_chan,
    fidl::ClientEnd<InstanceDevice> instance_client) {
  ASSERT_NO_FATAL_FAILURES(WaitForEvent(lifecycle_chan, Event::Open));

  auto endpoints = fidl::CreateEndpoints<Lifecycle>();
  ASSERT_OK(endpoints.status_value());
  auto [instance_lifecycle_chan, remote] = *std::move(endpoints);
  {
    auto result = InstanceDevice::Call::SubscribeToLifecycle(instance_client, std::move(remote));
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
  auto endpoints = fidl::CreateEndpoints<Lifecycle>();
  ASSERT_OK(endpoints.status_value());
  auto [lifecycle_chan, remote] = *std::move(endpoints);

  auto result = TestDevice::Call::CreateDevice(device_, std::move(remote), zx::channel{});
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  // There shouldn't be any pending events yet
  ASSERT_FALSE(AreEventsPending(lifecycle_chan));

  fidl::ClientEnd<InstanceDevice> instance_client;
  {
    fbl::unique_fd fd;
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), "sys/platform/11:12:0/instance-test/child", &fd));
    ASSERT_GT(fd.get(), 0);
    ASSERT_OK(
        fdio_get_service_handle(fd.release(), instance_client.channel().reset_and_get_address()));
  }

  ASSERT_NO_FATAL_FAILURES(
      VerifyPostOpenLifecycleViaClose(lifecycle_chan, std::move(instance_client)));
}

// Test the lifecycle of an instance device that's obtained via device_add
TEST_F(InstanceLifecycleTest, PipelinedClientClose) {
  // Subscribe to the device lifecycle events.
  auto lifecycle_endpoints = fidl::CreateEndpoints<Lifecycle>();
  ASSERT_OK(lifecycle_endpoints.status_value());
  auto [lifecycle_chan, lifecycle_remote] = *std::move(lifecycle_endpoints);

  auto instance_endpoints = fidl::CreateEndpoints<InstanceDevice>();
  ASSERT_OK(instance_endpoints.status_value());
  auto [instance_client, instance_client_remote] = *std::move(instance_endpoints);

  auto result = TestDevice::Call::CreateDevice(device_, std::move(lifecycle_remote),
                                               instance_client_remote.TakeChannel());
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  ASSERT_NO_FATAL_FAILURES(
      VerifyPostOpenLifecycleViaClose(lifecycle_chan, std::move(instance_client)));
}

// Test the lifecycle of an instance device that's obtained via fuchsia.io/Open
TEST_F(InstanceLifecycleTest, NonPipelinedClientRemoveAndClose) {
  // Subscribe to the device lifecycle events.
  auto endpoints = fidl::CreateEndpoints<Lifecycle>();
  ASSERT_OK(endpoints.status_value());
  auto [lifecycle_chan, remote] = *std::move(endpoints);

  auto result = TestDevice::Call::CreateDevice(device_, std::move(remote), zx::channel{});
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  // There shouldn't be any pending events yet
  ASSERT_FALSE(AreEventsPending(lifecycle_chan));

  fidl::ClientEnd<InstanceDevice> instance_client;
  {
    fbl::unique_fd fd;
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), "sys/platform/11:12:0/instance-test/child", &fd));
    ASSERT_GT(fd.get(), 0);
    ASSERT_OK(
        fdio_get_service_handle(fd.release(), instance_client.channel().reset_and_get_address()));
  }

  ASSERT_NO_FATAL_FAILURES(
      VerifyPostOpenLifecycleViaRemove(lifecycle_chan, std::move(instance_client)));
}

// Test the lifecycle of an instance device that's obtained via device_add
TEST_F(InstanceLifecycleTest, PipelinedClientRemoveAndClose) {
  // Subscribe to the device lifecycle events.
  auto lifecycle_endpoints = fidl::CreateEndpoints<Lifecycle>();
  ASSERT_OK(lifecycle_endpoints.status_value());
  auto [lifecycle_chan, lifecycle_remote] = *std::move(lifecycle_endpoints);

  auto instance_endpoints = fidl::CreateEndpoints<InstanceDevice>();
  ASSERT_OK(instance_endpoints.status_value());
  auto [instance_client, instance_client_remote] = *std::move(instance_endpoints);

  auto result = TestDevice::Call::CreateDevice(device_, std::move(lifecycle_remote),
                                               instance_client_remote.TakeChannel());
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  ASSERT_NO_FATAL_FAILURES(
      VerifyPostOpenLifecycleViaRemove(lifecycle_chan, std::move(instance_client)));
}

}  // namespace
