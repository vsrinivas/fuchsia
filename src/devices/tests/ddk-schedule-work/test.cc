// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/schedule/work/test/llcpp/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;
using llcpp::fuchsia::device::schedule::work::test::OwnedChannelDevice;
using llcpp::fuchsia::device::schedule::work::test::TestDevice;

class ScheduleWorkTest : public zxtest::Test {
 public:
  ~ScheduleWorkTest() override = default;
  void SetUp() override {
    IsolatedDevmgr::Args args;
    args.load_drivers.push_back("/boot/driver/ddk-schedule-work-test.so");

    board_test::DeviceEntry dev = {};
    dev.vid = PDEV_VID_TEST;
    dev.pid = PDEV_PID_SCHEDULE_WORK_TEST;
    dev.did = 0;
    args.device_list.push_back(dev);

    zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr_);
    ASSERT_OK(status);
    fbl::unique_fd fd;
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), "sys/platform/11:0d:0/schedule-work-test", &fd));
    ASSERT_GT(fd.get(), 0);
    ASSERT_OK(fdio_get_service_handle(fd.release(), chan_.reset_and_get_address()));
    ASSERT_NE(chan_.get(), ZX_HANDLE_INVALID);
  }

  void WaitDone() {
    auto result = TestDevice::Call::GetDoneEvent(zx::unowned(chan_));
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());

    zx::event done(std::move(result->result.mutable_response().event));

    zx_signals_t pending;
    ASSERT_OK(done.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), &pending));
  }

 protected:
  zx::channel chan_;
  IsolatedDevmgr devmgr_;
};

TEST_F(ScheduleWorkTest, ScheduleWork) {
  auto result = TestDevice::Call::ScheduleWork(zx::unowned(chan_), 1, 1);
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  ASSERT_NO_FATAL_FAILURES(WaitDone());

  auto result2 = TestDevice::Call::ScheduledWorkRan(zx::unowned(chan_));
  ASSERT_OK(result2.status());
  ASSERT_EQ(result2->work_items_run, 1);
}

TEST_F(ScheduleWorkTest, ScheduleWorkManyItemsSingleBatch) {
  auto result = TestDevice::Call::ScheduleWork(zx::unowned(chan_), 100, 100);
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  ASSERT_NO_FATAL_FAILURES(WaitDone());

  auto result2 = TestDevice::Call::ScheduledWorkRan(zx::unowned(chan_));
  ASSERT_OK(result2.status());
  ASSERT_EQ(result2->work_items_run, 100);
}

TEST_F(ScheduleWorkTest, ScheduleWorkManyItemsManyBatches) {
  auto result = TestDevice::Call::ScheduleWork(zx::unowned(chan_), 10, 100);
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  ASSERT_NO_FATAL_FAILURES(WaitDone());

  auto result2 = TestDevice::Call::ScheduledWorkRan(zx::unowned(chan_));
  ASSERT_OK(result2.status());
  ASSERT_EQ(result2->work_items_run, 100);
}

TEST_F(ScheduleWorkTest, ScheduleWorkDifferentThread) {
  auto result = TestDevice::Call::ScheduleWorkDifferentThread(zx::unowned(chan_));
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  ASSERT_NO_FATAL_FAILURES(WaitDone());

  auto result2 = TestDevice::Call::ScheduledWorkRan(zx::unowned(chan_));
  ASSERT_OK(result2.status());
  ASSERT_EQ(result2->work_items_run, 1);
}

TEST_F(ScheduleWorkTest, ScheduleWorkAsyncLoop) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  auto result = TestDevice::Call::GetChannel(zx::unowned(chan_), std::move(remote));
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  auto result2 = OwnedChannelDevice::Call::ScheduleWork(zx::unowned(local), 1, 1);
  ASSERT_OK(result2.status());
  ASSERT_FALSE(result2->result.is_err());
}

TEST_F(ScheduleWorkTest, ScheduleWorkAsyncLoopManyItemsSingleBatch) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  auto result = TestDevice::Call::GetChannel(zx::unowned(chan_), std::move(remote));
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  auto result2 = OwnedChannelDevice::Call::ScheduleWork(zx::unowned(local), 1000, 1000);
  ASSERT_OK(result2.status());
  ASSERT_FALSE(result2->result.is_err());
}

TEST_F(ScheduleWorkTest, ScheduleWorkAsyncLoopManyItemsManyBatches) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  auto result = TestDevice::Call::GetChannel(zx::unowned(chan_), std::move(remote));
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  auto result2 = OwnedChannelDevice::Call::ScheduleWork(zx::unowned(local), 10, 1000);
  ASSERT_OK(result2.status());
  ASSERT_FALSE(result2->result.is_err());
}
