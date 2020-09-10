// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/schedule/work/test/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>

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

using llcpp::fuchsia::device::schedule::work::test::LatencyHistogram;
using llcpp::fuchsia::device::schedule::work::test::OwnedChannelDevice;
using llcpp::fuchsia::device::schedule::work::test::TestDevice;

class TestScheduleWorkDriver;
using DeviceType = ddk::Device<TestScheduleWorkDriver, ddk::Unbindable, ddk::Messageable>;

class TestScheduleWorkDriver : public DeviceType, public TestDevice::Interface {
 public:
  struct WorkItemCtx {
    zx::time start;
    TestScheduleWorkDriver* parent;
  };

  TestScheduleWorkDriver(zx_device_t* parent)
      : DeviceType(parent), loop_(&kAsyncLoopConfigNeverAttachToThread) {
    loop_.StartThread("schedule-work-test-loop");
  }

  ~TestScheduleWorkDriver() { loop_.Shutdown(); }

  zx_status_t Bind();

  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  void ScheduleWork(uint32_t batch_size, uint32_t num_work_items,
                    ScheduleWorkCompleter::Sync completer) override;
  void ScheduleWorkDifferentThread(ScheduleWorkDifferentThreadCompleter::Sync completer) override;
  void GetDoneEvent(GetDoneEventCompleter::Sync completer) override;
  void ScheduledWorkRan(ScheduledWorkRanCompleter::Sync completer) override;
  void GetChannel(zx::channel request, GetChannelCompleter::Sync completer) override;

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    ::llcpp::fuchsia::device::schedule::work::test::TestDevice::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

  static void DoWork(void* ctx) {
    auto context = std::unique_ptr<WorkItemCtx>(static_cast<WorkItemCtx*>(ctx));
    context->parent->WorkItemCompletion(std::move(context));
  }

 private:
  void WorkItemCompletion(std::unique_ptr<WorkItemCtx> work_item_ctx) {
    work_items_ran_++;

    zx::duration duration = zx::clock::get_monotonic() - work_item_ctx->start;
    if (duration < zx::nsec(100)) {
      histogram_.buckets[0]++;
    } else if (duration < zx::nsec(250)) {
      histogram_.buckets[1]++;
    } else if (duration < zx::nsec(500)) {
      histogram_.buckets[2]++;
    } else if (duration < zx::usec(1)) {
      histogram_.buckets[3]++;
    } else if (duration < zx::usec(2)) {
      histogram_.buckets[4]++;
    } else if (duration < zx::usec(4)) {
      histogram_.buckets[5]++;
    } else if (duration < zx::usec(7)) {
      histogram_.buckets[6]++;
    } else if (duration < zx::usec(15)) {
      histogram_.buckets[7]++;
    } else if (duration < zx::usec(30)) {
      histogram_.buckets[8]++;
    } else {
      histogram_.buckets[9]++;
    }

    if (work_items_ran_ == work_items_expected_) {
      ZX_ASSERT(done_event_.signal(0, ZX_USER_SIGNAL_0) == ZX_OK);
    }

    if (work_items_left_ > 0) {
      work_item_ctx->start = zx::clock::get_monotonic();
      if (DdkScheduleWork(DoWork, work_item_ctx.get()) == ZX_OK) {
        work_items_left_--;
        work_item_ctx.release();
      }
    }
  }

  class Connection : public OwnedChannelDevice::Interface {
   public:
    struct WorkItemCtx {
      zx::time start;
      Connection* parent;
    };

    Connection(TestScheduleWorkDriver* parent) : parent_(parent) {}

    zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
      return fidl::BindSingleInFlightOnly(dispatcher, std::move(request), this);
    }

    void ScheduleWork(uint32_t batch_size, uint32_t num_work_items,
                      ScheduleWorkCompleter::Sync completer) override;

    static void DoWork(void* ctx) {
      auto context = std::unique_ptr<WorkItemCtx>(static_cast<WorkItemCtx*>(ctx));
      context->parent->WorkItemCompletion(std::move(context));
    }

   private:
    void WorkItemCompletion(std::unique_ptr<WorkItemCtx> work_item_ctx) {
      work_items_ran_++;

      zx::duration duration = zx::clock::get_monotonic() - work_item_ctx->start;
      if (duration < zx::nsec(100)) {
        histogram_.buckets[0]++;
      } else if (duration < zx::nsec(250)) {
        histogram_.buckets[1]++;
      } else if (duration < zx::nsec(500)) {
        histogram_.buckets[2]++;
      } else if (duration < zx::usec(1)) {
        histogram_.buckets[3]++;
      } else if (duration < zx::usec(2)) {
        histogram_.buckets[4]++;
      } else if (duration < zx::usec(4)) {
        histogram_.buckets[5]++;
      } else if (duration < zx::usec(7)) {
        histogram_.buckets[6]++;
      } else if (duration < zx::usec(15)) {
        histogram_.buckets[7]++;
      } else if (duration < zx::usec(30)) {
        histogram_.buckets[8]++;
      } else {
        histogram_.buckets[9]++;
      }
      if (work_items_ran_ == work_items_expected_) {
        sync_completion_signal(&completion_);
      }

      if (work_items_left_ > 0) {
        work_item_ctx->start = zx::clock::get_monotonic();
        if (parent_->DdkScheduleWork(DoWork, work_item_ctx.get()) == ZX_OK) {
          work_items_left_--;
          work_item_ctx.release();
        }
      }
    }

    uint32_t work_items_left_ = 0;
    uint32_t work_items_ran_ = 0;
    uint32_t work_items_expected_ = 0;
    LatencyHistogram histogram_;
    TestScheduleWorkDriver* parent_;
    sync_completion_t completion_;
  };

  async::Loop loop_;
  zx::event done_event_;
  std::vector<std::unique_ptr<Connection>> open_connections_;
  uint32_t work_items_left_ = 0;
  uint32_t work_items_ran_ = 0;
  uint32_t work_items_expected_ = 0;
  LatencyHistogram histogram_;
};

zx_status_t TestScheduleWorkDriver::Bind() {
  if (auto status = zx::event::create(0, &done_event_); status != ZX_OK) {
    return status;
  }
  return DdkAdd("schedule-work-test");
}

void TestScheduleWorkDriver::ScheduleWork(uint32_t batch_size, uint32_t num_work_items,
                                          ScheduleWorkCompleter::Sync completer) {
  batch_size = std::min(batch_size, num_work_items);

  work_items_left_ = num_work_items - batch_size;
  work_items_expected_ = num_work_items;

  for (uint32_t i = 0; i < batch_size; i++) {
    auto work_item_ctx = std::make_unique<WorkItemCtx>();
    work_item_ctx->start = zx::clock::get_monotonic();
    work_item_ctx->parent = this;

    auto status = DdkScheduleWork(DoWork, work_item_ctx.get());
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    work_item_ctx.release();
  }

  completer.ReplySuccess();
}

void TestScheduleWorkDriver::ScheduleWorkDifferentThread(
    ScheduleWorkDifferentThreadCompleter::Sync completer) {
  work_items_left_ = 0;
  work_items_expected_ = 1;

  zx_status_t status;
  std::thread thread([this, &status]() {
    auto work_item_ctx = std::make_unique<WorkItemCtx>();
    work_item_ctx->start = zx::clock::get_monotonic();
    work_item_ctx->parent = this;

    status = DdkScheduleWork(DoWork, work_item_ctx.get());
    if (status == ZX_OK) {
      work_item_ctx.release();
    }
  });
  thread.join();

  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void TestScheduleWorkDriver::GetDoneEvent(GetDoneEventCompleter::Sync completer) {
  zx::event dup;
  zx_status_t status = done_event_.duplicate(ZX_RIGHT_WAIT | ZX_RIGHT_TRANSFER, &dup);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(std::move(dup));
  }
}

void TestScheduleWorkDriver::ScheduledWorkRan(ScheduledWorkRanCompleter::Sync completer) {
  completer.Reply(work_items_ran_, histogram_);

  ZX_ASSERT(done_event_.signal(ZX_USER_SIGNAL_0, 0) == ZX_OK);
  work_items_ran_ = 0;
  histogram_ = {};
}

void TestScheduleWorkDriver::GetChannel(zx::channel request, GetChannelCompleter::Sync completer) {
  auto connection = std::make_unique<Connection>(this);
  auto status = connection->Connect(loop_.dispatcher(), std::move(request));
  if (status == ZX_OK) {
    open_connections_.push_back(std::move(connection));
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void TestScheduleWorkDriver::Connection::ScheduleWork(uint32_t batch_size, uint32_t num_work_items,
                                                      ScheduleWorkCompleter::Sync completer) {
  batch_size = std::min(batch_size, num_work_items);

  work_items_left_ = num_work_items - batch_size;
  work_items_expected_ = num_work_items;
  work_items_ran_ = 0;

  for (uint32_t i = 0; i < batch_size; i++) {
    auto work_item_ctx = std::make_unique<WorkItemCtx>();
    work_item_ctx->start = zx::clock::get_monotonic();
    work_item_ctx->parent = this;

    auto status = parent_->DdkScheduleWork(DoWork, work_item_ctx.get());
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    work_item_ctx.release();
  }

  if (batch_size > 0) {
    sync_completion_wait(&completion_, ZX_TIME_INFINITE);
    sync_completion_reset(&completion_);
  }

  completer.ReplySuccess(histogram_);
  histogram_ = {};
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
