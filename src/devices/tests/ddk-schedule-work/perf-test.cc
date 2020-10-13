// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/schedule/work/test/llcpp/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <stdio.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <ddk/platform-defs.h>

using driver_integration_test::IsolatedDevmgr;
using llcpp::fuchsia::device::schedule::work::test::LatencyHistogram;
using llcpp::fuchsia::device::schedule::work::test::OwnedChannelDevice;
using llcpp::fuchsia::device::schedule::work::test::TestDevice;

class ScheduleWorkCaller {
 public:
  zx_status_t SetUp() {
    IsolatedDevmgr::Args args;
    args.load_drivers.push_back("/boot/driver/ddk-schedule-work-test.so");

    board_test::DeviceEntry dev = {};
    dev.vid = PDEV_VID_TEST;
    dev.pid = PDEV_PID_SCHEDULE_WORK_TEST;
    dev.did = 0;
    args.device_list.push_back(dev);

    zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr_);
    if (status != ZX_OK) {
      return status;
    }

    fbl::unique_fd fd;
    status = devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), "sys/platform/11:0d:0/schedule-work-test", &fd);
    if (status != ZX_OK) {
      return status;
    }

    status = fdio_get_service_handle(fd.release(), chan_.reset_and_get_address());
    if (status != ZX_OK) {
      return status;
    }

    return ZX_OK;
  }

  zx_status_t WaitDone() {
    auto result = TestDevice::Call::GetDoneEvent(zx::unowned(chan_));
    if (result.status() != ZX_OK) {
      return result.status();
    }
    if (result->result.is_err()) {
      return result->result.err();
    }

    zx::event done(std::move(result->result.mutable_response().event));
    return done.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr);
  }

  zx_status_t ScheduleWorkPerf(uint32_t batch_size, uint32_t num_work_items) {
    auto result = TestDevice::Call::ScheduleWork(zx::unowned(chan_), batch_size, num_work_items);
    if (result.status() != ZX_OK) {
      return result.status();
    }
    if (result->result.is_err()) {
      return result->result.err();
    }

    if (auto status = WaitDone(); status != ZX_OK) {
      return status;
    }

    auto result2 = TestDevice::Call::ScheduledWorkRan(zx::unowned(chan_));
    if (result2.status() != ZX_OK) {
      return result2.status();
    }
    if (result2->work_items_run != num_work_items) {
      return ZX_ERR_INTERNAL;
    }

    printf("==%s== : batch: %d total: %d\n", __func__, batch_size, num_work_items);
    PrintHistogram(result2->histogram);
    return ZX_OK;
  }

  zx_status_t ScheduleWorkPerfDifferentThread() {
    auto result = TestDevice::Call::ScheduleWorkDifferentThread(zx::unowned(chan_));
    if (result.status() != ZX_OK) {
      return result.status();
    }
    if (result->result.is_err()) {
      return result->result.err();
    }

    if (auto status = WaitDone(); status != ZX_OK) {
      return status;
    }

    auto result2 = TestDevice::Call::ScheduledWorkRan(zx::unowned(chan_));
    if (result2.status() != ZX_OK) {
      return result2.status();
    }
    if (result2->work_items_run != 1) {
      return ZX_ERR_INTERNAL;
    }

    printf("==%s== : batch: 1 total: 1\n", __func__);
    PrintHistogram(result2->histogram);
    return ZX_OK;
  }

  zx_status_t ScheduleWorkPerfAsyncLoop(uint32_t batch_size, uint32_t num_work_items) {
    zx::channel local, remote;
    auto status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return status;
    }

    auto result = TestDevice::Call::GetChannel(zx::unowned(chan_), std::move(remote));
    if (result.status() != ZX_OK) {
      return result.status();
    }
    if (result->result.is_err()) {
      return result->result.err();
    }

    auto result2 =
        OwnedChannelDevice::Call::ScheduleWork(zx::unowned(local), batch_size, num_work_items);
    if (result2.status() != ZX_OK) {
      return result2.status();
    }
    if (result2->result.is_err()) {
      return result2->result.err();
    }

    printf("==%s== : batch: %d total: %d\n", __func__, batch_size, num_work_items);
    PrintHistogram(result2->result.response().histogram);
    return ZX_OK;
  }

  zx_status_t ScheduleWorkPerfAsyncLoop2(uint32_t batch_size, uint32_t num_work_items) {
    zx::channel local, remote;
    auto status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return status;
    }

    auto result = TestDevice::Call::GetChannel(zx::unowned(chan_), std::move(remote));
    if (result.status() != ZX_OK) {
      return result.status();
    }
    if (result->result.is_err()) {
      return result->result.err();
    }

    LatencyHistogram histogram;
    auto work_items_left = num_work_items;
    do {
      auto real_batch_size = std::min(batch_size, work_items_left);

      auto result2 = OwnedChannelDevice::Call::ScheduleWork(zx::unowned(local), 1, real_batch_size);
      if (result2.status() != ZX_OK) {
        return result2.status();
      }
      if (result2->result.is_err()) {
        return result2->result.err();
      }
      MergeHistograms(&histogram, result2->result.response().histogram);

      work_items_left -= real_batch_size;
    } while (work_items_left > 0);

    printf("==%s== : batch: %d total: %d\n", __func__, batch_size, num_work_items);
    PrintHistogram(histogram);
    return ZX_OK;
  }

 private:
  static void MergeHistograms(LatencyHistogram* to, const LatencyHistogram& from) {
    for (size_t i = 0; i < 10; i++) {
      to->buckets[i] += from.buckets[i];
    }
  }

  static void PrintHistogram(const LatencyHistogram& histogram) {
    printf("[0ns, 100ns):      %ld\n", histogram.buckets[0]);
    printf("[100ns, 250ns):    %ld\n", histogram.buckets[1]);
    printf("[250ns, 500ns):    %ld\n", histogram.buckets[2]);
    printf("[500ns, 1us):      %ld\n", histogram.buckets[3]);
    printf("[1us, 2us):        %ld\n", histogram.buckets[4]);
    printf("[2us, 4us):        %ld\n", histogram.buckets[5]);
    printf("[4us, 7us):        %ld\n", histogram.buckets[6]);
    printf("[7us, 15us):       %ld\n", histogram.buckets[7]);
    printf("[15us, 30us):      %ld\n", histogram.buckets[8]);
    printf("[30us, infinity):  %ld\n\n", histogram.buckets[9]);
  }

  zx::channel chan_;
  IsolatedDevmgr devmgr_;
};

int main() {
  ScheduleWorkCaller caller;
  zx_status_t status = caller.SetUp();
  if (status != ZX_OK) {
    return -1;
  }
  if ((status = caller.ScheduleWorkPerf(1, 1000)) != ZX_OK ||
      (status = caller.ScheduleWorkPerf(5, 1000)) != ZX_OK ||
      (status = caller.ScheduleWorkPerf(10, 1000)) != ZX_OK ||
      (status = caller.ScheduleWorkPerf(20, 1000)) != ZX_OK ||
      (status = caller.ScheduleWorkPerf(1000, 1000)) != ZX_OK ||
      (status = caller.ScheduleWorkPerfDifferentThread()) != ZX_OK ||
      (status = caller.ScheduleWorkPerfAsyncLoop(1, 1000)) != ZX_OK ||
      (status = caller.ScheduleWorkPerfAsyncLoop(5, 1000)) != ZX_OK ||
      (status = caller.ScheduleWorkPerfAsyncLoop(10, 1000)) != ZX_OK ||
      (status = caller.ScheduleWorkPerfAsyncLoop(20, 1000)) != ZX_OK ||
      (status = caller.ScheduleWorkPerfAsyncLoop(1000, 1000)) != ZX_OK ||
      (status = caller.ScheduleWorkPerfAsyncLoop2(1, 1000)) != ZX_OK ||
      (status = caller.ScheduleWorkPerfAsyncLoop2(5, 1000)) != ZX_OK ||
      (status = caller.ScheduleWorkPerfAsyncLoop2(10, 1000)) != ZX_OK ||
      (status = caller.ScheduleWorkPerfAsyncLoop2(20, 1000)) != ZX_OK) {
    return -2;
  }

  return 0;
}
