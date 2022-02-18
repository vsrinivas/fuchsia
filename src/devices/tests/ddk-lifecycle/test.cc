// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.device.lifecycle.test/cpp/wire.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.serial/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/ddk/platform-defs.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/channel.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <vector>

#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;
using fuchsia_device::Controller;
using fuchsia_device_lifecycle_test::Lifecycle;
using fuchsia_device_lifecycle_test::TestDevice;
using fuchsia_hardware_serial::Device;
using fuchsia_io::Directory;
using fuchsia_io::File;

class LifecycleTest : public zxtest::Test {
 public:
  ~LifecycleTest() override = default;
  void SetUp() override {
    IsolatedDevmgr::Args args;

    board_test::DeviceEntry dev = {};
    dev.vid = PDEV_VID_TEST;
    dev.pid = PDEV_PID_LIFECYCLE_TEST;
    dev.did = 0;
    args.device_list.push_back(dev);

    zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr_);
    ASSERT_OK(status);
    fbl::unique_fd fd;
    ASSERT_OK(device_watcher::RecursiveWaitForFile(devmgr_.devfs_root(),
                                                   "sys/platform/11:10:0/ddk-lifecycle-test", &fd));
    ASSERT_GT(fd.get(), 0);

    ASSERT_OK(fdio_get_service_handle(fd.release(), chan_.channel().reset_and_get_address()));
    ASSERT_TRUE(chan_.is_valid());

    // Subscribe to the device lifecycle events.
    auto endpoints = fidl::CreateEndpoints<Lifecycle>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = *std::move(endpoints);

    auto result = fidl::WireCall<TestDevice>(chan_)->SubscribeToLifecycle(std::move(remote));
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());
    lifecycle_chan_ = std::move(local);
  }

 protected:
  void WaitPreRelease(uint64_t child_id);

  fidl::ClientEnd<TestDevice> chan_;
  IsolatedDevmgr devmgr_;

  fidl::ClientEnd<Lifecycle> lifecycle_chan_;
};

void LifecycleTest::WaitPreRelease(uint64_t child_id) {
  class EventHandler : public fidl::WireSyncEventHandler<Lifecycle> {
   public:
    EventHandler() = default;

    bool removed() const { return removed_; }
    uint64_t device_id() const { return device_id_; }

    void OnChildPreRelease(fidl::WireEvent<Lifecycle::OnChildPreRelease>* event) override {
      device_id_ = event->child_id;
      removed_ = true;
    }

    zx_status_t Unknown() override { return ZX_ERR_NOT_SUPPORTED; }

   private:
    bool removed_ = false;
    uint64_t device_id_ = 0;
  };

  EventHandler event_handler;
  while (!event_handler.removed()) {
    ASSERT_OK(event_handler.HandleOneEvent(lifecycle_chan_).status());
  }
  ASSERT_EQ(event_handler.device_id(), child_id);
}

TEST_F(LifecycleTest, ChildPreRelease) {
  // Add some child devices and store the returned ids.
  std::vector<uint64_t> child_ids;
  const uint32_t num_children = 10;
  for (unsigned int i = 0; i < num_children; i++) {
    auto result = fidl::WireCall<TestDevice>(chan_)->AddChild(true /* complete_init */,
                                                              ZX_OK /* init_status */);
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());
    child_ids.push_back(result->result.response().child_id);
  }

  // Remove the child devices and check the test device received the pre-release notifications.
  for (auto child_id : child_ids) {
    auto result = fidl::WireCall<TestDevice>(chan_)->RemoveChild(child_id);
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());

    // Wait for the child pre-release notification.
    ASSERT_NO_FATAL_FAILURE(WaitPreRelease(child_id));
  }
}

TEST_F(LifecycleTest, Init) {
  // Add a child device that does not complete its init hook yet.
  uint64_t child_id;
  auto result = fidl::WireCall<TestDevice>(chan_)->AddChild(false /* complete_init */,
                                                            ZX_OK /* init_status */);
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());
  child_id = result->result.response().child_id;

  auto remove_result = fidl::WireCall<TestDevice>(chan_)->RemoveChild(child_id);
  ASSERT_OK(remove_result.status());
  ASSERT_FALSE(remove_result->result.is_err());

  auto init_result = fidl::WireCall<TestDevice>(chan_)->CompleteChildInit(child_id);
  ASSERT_OK(init_result.status());
  ASSERT_FALSE(init_result->result.is_err());

  // Wait for the child pre-release notification.
  ASSERT_NO_FATAL_FAILURE(WaitPreRelease(child_id));
}

TEST_F(LifecycleTest, CloseAllConnectionsOnInstanceUnbind) {
  auto result = fidl::WireCall<TestDevice>(chan_)->AddChild(true /* complete_init */,
                                                            ZX_OK /* init_status */);
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());
  auto child_id = result->result.response().child_id;
  fbl::unique_fd fd;
  ASSERT_OK(device_watcher::RecursiveWaitForFile(
      devmgr_.devfs_root(), "sys/platform/11:10:0/ddk-lifecycle-test/ddk-lifecycle-test-child",
      &fd));
  ASSERT_TRUE(fd.get() > 0);
  zx::channel chan;
  fdio_get_service_handle(fd.get(), chan.reset_and_get_address());
  ASSERT_TRUE(fidl::WireCall<TestDevice>(chan_)->RemoveChild(child_id).ok());
  zx_signals_t closed;
  ASSERT_OK(chan.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &closed));
  ASSERT_TRUE(closed & ZX_CHANNEL_PEER_CLOSED);
  // Wait for the child pre-release notification.
  ASSERT_NO_FATAL_FAILURE(WaitPreRelease(child_id));
}

TEST_F(LifecycleTest, ReadCallFailsDuringUnbind) {
  auto result = fidl::WireCall<TestDevice>(chan_)->AddChild(true /* complete_init */,
                                                            ZX_OK /* init_status */);
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());
  auto child_id = result->result.response().child_id;
  fbl::unique_fd fd;

  ASSERT_OK(device_watcher::RecursiveWaitForFile(
      devmgr_.devfs_root(), "sys/platform/11:10:0/ddk-lifecycle-test/ddk-lifecycle-test-child",
      &fd));
  ASSERT_TRUE(fd.get() > 0);
  fidl::ClientEnd<File> chan;
  fdio_get_service_handle(fd.get(), chan.channel().reset_and_get_address());

  ASSERT_TRUE(fidl::WireCall<TestDevice>(chan_)->AsyncRemoveChild(child_id).ok());
  {
    const fidl::WireResult read_result = fidl::WireCall<File>(chan)->Read(10);
    ASSERT_OK(read_result.status());
    const fidl::WireResponse response = read_result.value();
    ASSERT_TRUE(response.result.is_err());
    ASSERT_STATUS(response.result.err(), ZX_ERR_IO_NOT_PRESENT);
  }
  {
    std::array<uint8_t, 5> array;
    const fidl::WireResult write_result =
        fidl::WireCall<File>(chan)->Write(fidl::VectorView<uint8_t>::FromExternal(array));
    ASSERT_OK(write_result.status());
    const fidl::WireResponse response = write_result.value();
    ASSERT_TRUE(response.result.is_err());
    ASSERT_STATUS(response.result.err(), ZX_ERR_IO_NOT_PRESENT);
  }
  int fd2 = open("sys/platform/11:10:0/ddk-lifecycle-test/ddk-lifecycle-test-child", O_RDWR);
  ASSERT_EQ(fd2, -1);
  ASSERT_EQ(fidl::WireCall<Device>(fidl::UnownedClientEnd<Device>(chan.channel().borrow()))
                ->GetClass()
                .status(),
            ZX_ERR_PEER_CLOSED);
  struct Epitaph {
    zx_txid_t txid;
    uint8_t flags[3];
    uint8_t magic_number;
    uint64_t ordinal;
    zx_status_t error;
  } epitaph;
  constexpr auto kEpitaph = 0xFFFFFFFFFFFFFFFF;
  uint32_t actual_bytes = 0;
  uint32_t actual_handles = 0;
  ASSERT_OK(chan.channel().read(0, &epitaph, nullptr, sizeof(epitaph), 0, &actual_bytes,
                                &actual_handles));
  ASSERT_EQ(actual_bytes, sizeof(epitaph));
  ASSERT_EQ(epitaph.ordinal, kEpitaph);
  ASSERT_EQ(epitaph.error, ZX_ERR_IO_NOT_PRESENT);
}

TEST_F(LifecycleTest, CloseAllConnectionsOnUnbind) {
  fidl::WireCall<Controller>(fidl::UnownedClientEnd<Controller>(chan_.channel().borrow()))
      ->ScheduleUnbind();
  zx_signals_t closed;
  ASSERT_OK(chan_.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &closed));
  ASSERT_TRUE(closed & ZX_CHANNEL_PEER_CLOSED);
}

// Tests that the child device is removed if init fails.
TEST_F(LifecycleTest, FailedInit) {
  uint64_t child_id;
  auto result = fidl::WireCall<TestDevice>(chan_)->AddChild(true /* complete_init */,
                                                            ZX_ERR_BAD_STATE /* init_status */);
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());
  child_id = result->result.response().child_id;

  // Wait for the child pre-release notification.
  ASSERT_NO_FATAL_FAILURE(WaitPreRelease(child_id));
}
