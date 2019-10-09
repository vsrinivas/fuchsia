// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/schedule/work/test/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>

#include <memory>
#include <thread>
#include <vector>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>

namespace {

using llcpp::fuchsia::device::schedule::work::test::OwnedChannelDevice;
using llcpp::fuchsia::device::schedule::work::test::TestDevice;

class TestScheduleWorkDriver;
using DeviceType = ddk::Device<TestScheduleWorkDriver, ddk::UnbindableNew, ddk::Messageable>;

class TestScheduleWorkDriver : public DeviceType, public TestDevice::Interface {
 public:
  TestScheduleWorkDriver(zx_device_t* parent)
      : DeviceType(parent), loop_(&kAsyncLoopConfigNoAttachToThread) {
    loop_.StartThread("schedule-work-test-loop");
  }

  ~TestScheduleWorkDriver() {
    loop_.Shutdown();
  }

  zx_status_t Bind();

  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  void ScheduleWork(ScheduleWorkCompleter::Sync completer) override;
  void ScheduleWorkDifferentThread(ScheduleWorkDifferentThreadCompleter::Sync completer) override;
  void ScheduledWorkRan(ScheduledWorkRanCompleter::Sync completer) override;
  void GetChannel(zx::channel request, GetChannelCompleter::Sync completer) override;

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    ::llcpp::fuchsia::device::schedule::work::test::TestDevice::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

 private:
  class Connection : public OwnedChannelDevice::Interface {
   public:
    Connection(TestScheduleWorkDriver* parent) : parent_(parent) {}

    zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
      return fidl::Bind(dispatcher, std::move(request), this);
    }

    void ScheduleWork(ScheduleWorkCompleter::Sync completer) override;

   private:
    TestScheduleWorkDriver* parent_;
    sync_completion_t completion_;
  };

  async::Loop loop_;
  std::vector<std::unique_ptr<Connection>> open_connections_;
  bool ran_ = false;
};

zx_status_t TestScheduleWorkDriver::Bind() { return DdkAdd("schedule-work-test"); }

void TestScheduleWorkDriver::ScheduleWork(ScheduleWorkCompleter::Sync completer) {
  auto status = DdkScheduleWork(
      [](void* ctx) { static_cast<TestScheduleWorkDriver*>(ctx)->ran_ = true; }, this);

  ::llcpp::fuchsia::device::schedule::work::test::TestDevice_ScheduleWork_Result result;
  if (status != ZX_OK) {
    result.set_err(status);
  } else {
    result.set_response(
        ::llcpp::fuchsia::device::schedule::work::test::TestDevice_ScheduleWork_Response{});
  }

  completer.Reply(std::move(result));
}

void TestScheduleWorkDriver::ScheduleWorkDifferentThread(
    ScheduleWorkDifferentThreadCompleter::Sync completer) {
  zx_status_t status;
  std::thread thread([this, &status]() {
    status = DdkScheduleWork(
        [](void* ctx) { static_cast<TestScheduleWorkDriver*>(ctx)->ran_ = true; }, this);
  });
  thread.join();

  ::llcpp::fuchsia::device::schedule::work::test::TestDevice_ScheduleWorkDifferentThread_Result
      result;
  if (status != ZX_OK) {
    result.set_err(status);
  } else {
    result.set_response(::llcpp::fuchsia::device::schedule::work::test::
                            TestDevice_ScheduleWorkDifferentThread_Response{});
  }

  completer.Reply(std::move(result));
}

void TestScheduleWorkDriver::ScheduledWorkRan(ScheduledWorkRanCompleter::Sync completer) {
  completer.Reply(ran_);
  ran_ = false;
}

void TestScheduleWorkDriver::GetChannel(zx::channel request, GetChannelCompleter::Sync completer) {
  ::llcpp::fuchsia::device::schedule::work::test::TestDevice_GetChannel_Result result;

  auto connection = std::make_unique<Connection>(this);
  auto status = connection->Connect(loop_.dispatcher(), std::move(request));
  if (status == ZX_OK) {
    open_connections_.push_back(std::move(connection));
    result.set_response(
        ::llcpp::fuchsia::device::schedule::work::test::TestDevice_GetChannel_Response{});
  } else {
    result.set_err(status);
  }

  completer.Reply(std::move(result));
}

void TestScheduleWorkDriver::Connection::ScheduleWork(ScheduleWorkCompleter::Sync completer) {
  auto status = parent_->DdkScheduleWork(
      [](void* ctx) {
        sync_completion_signal(&static_cast<TestScheduleWorkDriver::Connection*>(ctx)->completion_);
      },
      this);

  ::llcpp::fuchsia::device::schedule::work::test::OwnedChannelDevice_ScheduleWork_Result result;
  if (status != ZX_OK) {
    result.set_err(status);
  } else {
    sync_completion_wait(&completion_, ZX_TIME_INFINITE);
    sync_completion_reset(&completion_);
    result.set_response(
        ::llcpp::fuchsia::device::schedule::work::test::OwnedChannelDevice_ScheduleWork_Response{});
  }

  completer.Reply(std::move(result));
}

zx_status_t TestScheduleWorkBind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestScheduleWorkDriver>(&ac, device);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

zx_driver_ops_t driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TestScheduleWorkBind;
  return ops;
}();

}  // namespace

// clang-format off
ZIRCON_DRIVER_BEGIN(TestScheduleWork, driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_SCHEDULE_WORK_TEST),
ZIRCON_DRIVER_END(TestScheduleWork)
    // clang-format on
