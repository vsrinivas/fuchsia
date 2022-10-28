// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mlan_mocks.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_bus.h"

namespace {

using wlan::nxpfmac::IoctlAdapter;
using wlan::nxpfmac::IoctlRequest;
using wlan::nxpfmac::IoctlStatus;

struct IoctlAdapterTest : public zxtest::Test {
  void SetUp() override {
    auto status = IoctlAdapter::Create(mock_mlan_.GetAdapter(), &mock_bus_);
    ASSERT_OK(status.status_value());
    ioctl_adapter_ = std::move(status.value());
  }

  wlan::nxpfmac::MlanMockAdapter mock_mlan_;
  wlan::nxpfmac::MockBus mock_bus_;
  std::unique_ptr<IoctlAdapter> ioctl_adapter_;
};

TEST_F(IoctlAdapterTest, IssueIoctlAsync) {
  // Test that an asynchronous ioctl completes correctly and calls the provided completion callback.

  constexpr uint32_t kBssIndex = 20;
  // Add some trailing space to make sure we verify that too.
  constexpr size_t kSomeTrailingSpace = 42;

  bool on_ioctl_called = false;
  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    EXPECT_EQ(kBssIndex, req->bss_index);
    on_ioctl_called = true;
    // Schedule the on complete to run asynchronously to simulate an asynchronous ioctl.
    ioctl_adapter_->OnIoctlComplete(req, IoctlStatus::Success);
    return MLAN_STATUS_PENDING;
  };

  mock_mlan_.SetOnMlanIoctl(std::move(on_ioctl));

  IoctlRequest<mlan_ds_bss, kSomeTrailingSpace> join_request(
      MLAN_IOCTL_BSS, MLAN_ACT_SET, kBssIndex,
      mlan_ds_bss{.sub_command = MLAN_OID_BSS_START,
                  .param = {.ssid_bssid = {.idx = 0, .channel = 11}}});

  // Verify that the request correctly allocated size and positioned it in the right place.
  ASSERT_EQ(kSomeTrailingSpace, join_request.TrailingSize());
  ASSERT_EQ(join_request.TrailingData(),
            reinterpret_cast<uint8_t *>(&join_request.UserReq()) + sizeof(mlan_ds_bss));

  sync_completion_t complete_called;
  // We can't capture join_request because it's variable sized, capture its pointer instead.
  auto on_complete = [join_request_addr = &join_request, &complete_called](mlan_ioctl_req *req,
                                                                           IoctlStatus status) {
    EXPECT_EQ(static_cast<void *>(req), static_cast<void *>(join_request_addr));
    sync_completion_signal(&complete_called);
  };

  ASSERT_EQ(IoctlStatus::Pending,
            ioctl_adapter_->IssueIoctl(&join_request, std::move(on_complete)));
  ASSERT_OK(sync_completion_wait(&complete_called, ZX_TIME_INFINITE));
  ASSERT_TRUE(on_ioctl_called);
}

TEST_F(IoctlAdapterTest, IssueIoctlSync) {
  constexpr uint32_t kBssIndex = 28;

  bool on_ioctl_called = false;
  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    EXPECT_EQ(kBssIndex, req->bss_index);
    on_ioctl_called = true;
    ioctl_adapter_->OnIoctlComplete(req, IoctlStatus::Success);
    return MLAN_STATUS_PENDING;
  };

  mock_mlan_.SetOnMlanIoctl(std::move(on_ioctl));

  IoctlRequest<mlan_ds_bss> join_request(MLAN_IOCTL_BSS, MLAN_ACT_SET, kBssIndex,
                                         mlan_ds_bss{
                                             .sub_command = MLAN_OID_BSS_START,
                                             .param = {.ssid_bssid = {.idx = 0, .channel = 11}},
                                         });

  ASSERT_EQ(IoctlStatus::Success, ioctl_adapter_->IssueIoctlSync(&join_request));
  ASSERT_TRUE(on_ioctl_called);
}

TEST_F(IoctlAdapterTest, IssueIoctlAsyncTimeout) {
  // Test that a pending ioctl that never completes properly triggers a timeout.

  bool called_with_set = false;
  bool called_with_cancel = false;
  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_SET) {
      called_with_set = true;
      // This ioctl call should never complete, we need it to time out.
      return MLAN_STATUS_PENDING;
    }
    if (req->action == MLAN_ACT_CANCEL) {
      called_with_cancel = true;
      req->status_code = MLAN_ERROR_CMD_CANCEL;
      // mlan calls the callback with failure at this point, emulate this behavior.
      ioctl_adapter_->OnIoctlComplete(req, IoctlStatus::Failure);
      return MLAN_STATUS_SUCCESS;
    }
    return MLAN_STATUS_SUCCESS;
  };

  mock_mlan_.SetOnMlanIoctl(std::move(on_ioctl));

  IoctlRequest<mlan_ds_scan> perform_scan(MLAN_IOCTL_SCAN, MLAN_ACT_SET, 0,
                                          mlan_ds_scan{.sub_command = MLAN_OID_SCAN_NORMAL});

  sync_completion_t completion;
  IoctlStatus ioctl_status;
  auto on_complete = [&](pmlan_ioctl_req req, IoctlStatus status) {
    ioctl_status = status;
    sync_completion_signal(&completion);
  };

  constexpr zx_duration_t kTimeout = ZX_MSEC(5);

  zx_time_t start = zx_clock_get_monotonic();
  ASSERT_EQ(IoctlStatus::Pending,
            ioctl_adapter_->IssueIoctl(&perform_scan, std::move(on_complete), kTimeout));
  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));
  zx_time_t end = zx_clock_get_monotonic();
  ASSERT_EQ(IoctlStatus::Timeout, ioctl_status);
  ASSERT_TRUE(called_with_set);
  ASSERT_TRUE(called_with_cancel);
  // Enough time should have passed before timeout.
  ASSERT_GE(end - start, kTimeout);
}

TEST_F(IoctlAdapterTest, IssueIoctlAsyncFailure) {
  // Test that a synchronously failed call to IssueIoctlAsync will not trigger a timeout.

  std::atomic<int> on_ioctl_calls = 0;
  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    ++on_ioctl_calls;
    return MLAN_STATUS_FAILURE;
  };

  mock_mlan_.SetOnMlanIoctl(std::move(on_ioctl));

  IoctlRequest<mlan_ds_scan> perform_scan(MLAN_IOCTL_SCAN, MLAN_ACT_SET, 0,
                                          mlan_ds_scan{.sub_command = MLAN_OID_SCAN_NORMAL});

  std::atomic<int> on_complete_calls = 0;
  auto on_complete = [&](pmlan_ioctl_req req, IoctlStatus status) { ++on_complete_calls; };

  constexpr zx_duration_t kTimeout = ZX_MSEC(1);

  ASSERT_EQ(IoctlStatus::Failure,
            ioctl_adapter_->IssueIoctl(&perform_scan, std::move(on_complete), kTimeout));

  // Sleep for an order of magnitude longer than the timeout. This might not always catch a
  // potential problem but it will the vast majority of times.
  zx_nanosleep(zx_deadline_after(kTimeout * 10));

  // on_complete should never have been called, the ioctl failed.
  ASSERT_EQ(0u, on_complete_calls.load());
  // on_ioctl should only have been called once, it failed and there's no need for cancelations
  ASSERT_EQ(1u, on_ioctl_calls.load());
}

TEST_F(IoctlAdapterTest, IssueIoctlCompleteBeforeTimeoutScheduled) {
  // Test the corner case of a pending ioctl completes before the timeout task for the ioctl can
  // even be scheduled.

  std::atomic<int> on_ioctl_calls = 0;
  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    ++on_ioctl_calls;
    // Schedule the completion before the ioctl even returns.
    ioctl_adapter_->OnIoctlComplete(req, IoctlStatus::Success);
    return MLAN_STATUS_PENDING;
  };

  mock_mlan_.SetOnMlanIoctl(std::move(on_ioctl));

  IoctlRequest<mlan_ds_scan> perform_scan(MLAN_IOCTL_SCAN, MLAN_ACT_SET, 0,
                                          mlan_ds_scan{.sub_command = MLAN_OID_SCAN_NORMAL});

  std::atomic<int> on_complete_calls = 0;
  IoctlStatus ioctl_status = IoctlStatus::Failure;
  sync_completion_t completion;
  auto on_complete = [&](pmlan_ioctl_req req, IoctlStatus status) {
    ioctl_status = status;
    ++on_complete_calls;
    sync_completion_signal(&completion);
  };

  constexpr zx_duration_t kTimeout = ZX_MSEC(1);

  ASSERT_EQ(IoctlStatus::Pending,
            ioctl_adapter_->IssueIoctl(&perform_scan, std::move(on_complete), kTimeout));

  // Sleep for an order of magnitude longer than the timeout. This might not always catch a
  // potential problem but it will the vast majority of times.
  zx_nanosleep(zx_deadline_after(kTimeout * 10));

  // Wait for completion to avoid potential flakiness.
  sync_completion_wait(&completion, ZX_TIME_INFINITE);

  // on_complete should have been called only once.
  ASSERT_EQ(1u, on_complete_calls.load());
  // on_ioctl should only have been called once, the timeout should not have made any calls.
  ASSERT_EQ(1u, on_ioctl_calls.load());
}

TEST_F(IoctlAdapterTest, IssueIoctlCompleteDuringCancelation) {
  // Test for the edge case where both timeout and completion are scheduled on the dispatcher.

  IoctlRequest<mlan_ds_rate> blocking_request(MLAN_IOCTL_RATE, MLAN_ACT_SET, 0, mlan_ds_rate{});
  IoctlRequest<mlan_ds_rate> request(MLAN_IOCTL_BSS, MLAN_ACT_SET, 0, mlan_ds_rate{});

  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_CANCEL) {
      // This is the cancelation because of the timeout, emulate mlan's behavior.
      req->status_code = MLAN_ERROR_CMD_CANCEL;
      ioctl_adapter_->OnIoctlComplete(req, IoctlStatus::Failure);
      return MLAN_STATUS_SUCCESS;
    }
    if (&blocking_request.IoctlReq() == req) {
      // This is the request that's intended to block until both the ioctl completion and timeout
      // for our main request have been scheduled. Make it succeed immediately, the completion
      // callback will do the waiting.
      return MLAN_STATUS_SUCCESS;
    }
    if (&request.IoctlReq() == req) {
      // This is the request that should be blocked and timed out. Don't schedule it's completion
      // until after the timeout has been properly scheduled.
      ioctl_adapter_->OnIoctlComplete(req, IoctlStatus::Success);
      return MLAN_STATUS_PENDING;
    }

    return MLAN_STATUS_SUCCESS;
  };

  mock_mlan_.SetOnMlanIoctl(std::move(on_ioctl));

  // Issue a request that will complete immediately and call the completion callback on the
  // ioctl_adapter's working thread. The callback will block until we're ready to proceed.
  sync_completion_t unblock_request;
  ASSERT_EQ(
      IoctlStatus::Pending,
      ioctl_adapter_->IssueIoctl(&blocking_request, [&](pmlan_ioctl_req req, IoctlStatus status) {
        sync_completion_wait(&unblock_request, ZX_TIME_INFINITE);
      }));

  sync_completion_t request_completed;
  std::atomic<int> completion_calls = 0;
  // Now issue an IOCTL that will first complete and then timeout afterwards. Because the blocking
  // request's callback has not yet completed, both of these tasks should be scheduled. Because this
  // relies on timing this test may not always trigger an error but it should most of the time.
  ASSERT_EQ(IoctlStatus::Pending,
            ioctl_adapter_->IssueIoctl(&request, [&](pmlan_ioctl_req req, IoctlStatus status) {
              ++completion_calls;
              sync_completion_signal(&request_completed);
            }));

  ASSERT_TRUE(ioctl_adapter_->CancelIoctl(&request));

  // At this point the completion and cancelation of the request should have been queued up. Let the
  // blocking request complete now and then both the completion and cancelation should go through.

  sync_completion_signal(&unblock_request);

  ASSERT_OK(sync_completion_wait(&request_completed, ZX_TIME_INFINITE));

  // Destroy the ioctl adapter here to ensure that whatever scheduled callback loses doesn't call
  // into a destroyed request. Destroying the ioctl adapter will stop the ioctl worker thread and
  // wait for all work to complete.
  ioctl_adapter_.reset();
}

TEST_F(IoctlAdapterTest, IssueIoctlSyncTimeout) {
  // Test that IssueIoctlSync correctly times out when provided with a timeout parameter.

  bool on_ioctl_called = false;
  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_CANCEL) {
      // Cancelation should emulate mlan behavior, set the status code, call the callback with
      // failure and indicate that the cancellation succeeded.
      req->status_code = MLAN_ERROR_CMD_CANCEL;
      ioctl_adapter_->OnIoctlComplete(req, IoctlStatus::Failure);
      return MLAN_STATUS_SUCCESS;
    }
    on_ioctl_called = true;
    // This ioctl implementation never calls the callback, it should timeout.
    return MLAN_STATUS_PENDING;
  };

  mock_mlan_.SetOnMlanIoctl(std::move(on_ioctl));

  IoctlRequest<mlan_ds_bss> join_request(MLAN_IOCTL_BSS, MLAN_ACT_SET, 0,
                                         mlan_ds_bss{
                                             .sub_command = MLAN_OID_BSS_START,
                                             .param = {.ssid_bssid = {.idx = 0, .channel = 11}},
                                         });

  constexpr zx_duration_t kTimeout = ZX_MSEC(5);
  zx_time_t start = zx_clock_get_monotonic();
  ASSERT_EQ(IoctlStatus::Timeout, ioctl_adapter_->IssueIoctlSync(&join_request, kTimeout));
  zx_time_t end = zx_clock_get_monotonic();
  ASSERT_TRUE(on_ioctl_called);
  // Enough time should have passed before timeout.
  ASSERT_GE(end - start, kTimeout);
}

TEST_F(IoctlAdapterTest, CancelIoctl) {
  // Test that CancelIoctl behaves as expected, modifying the canceled request action and issues
  // the correct ioctl.

  IoctlRequest<mlan_ds_scan> perform_scan(MLAN_IOCTL_SCAN, MLAN_ACT_SET, 0,
                                          mlan_ds_scan{.sub_command = MLAN_OID_SCAN_NORMAL});

  std::optional<uint32_t> on_ioctl_action;
  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    EXPECT_EQ(req, &perform_scan.IoctlReq());

    // The call should return pending for the set call and success for the cancel call. This will
    // make the request seem pending until it's canceled.
    if (req->action != MLAN_ACT_CANCEL) {
      return MLAN_STATUS_PENDING;
    }
    on_ioctl_action = req->action;
    // The cancel ioctl must indicate that the ioctl was successfully canceled for the cancel call
    // to return true.
    req->status_code = MLAN_ERROR_CMD_CANCEL;
    // A cancelation must appear as a failure in the ioctl complete call.
    ioctl_adapter_->OnIoctlComplete(req, IoctlStatus::Failure);
    return MLAN_STATUS_SUCCESS;
  };

  mock_mlan_.SetOnMlanIoctl(std::move(on_ioctl));

  sync_completion_t cancel_complete;
  auto on_cancel_complete = [&](pmlan_ioctl_req req, IoctlStatus status) {
    sync_completion_signal(&cancel_complete);
  };

  ASSERT_EQ(IoctlStatus::Pending, ioctl_adapter_->IssueIoctl(&perform_scan, on_cancel_complete));

  // Because the ioctl never completes we should be able to cancel it here.
  ASSERT_TRUE(ioctl_adapter_->CancelIoctl(&perform_scan));

  ASSERT_OK(sync_completion_wait(&cancel_complete, ZX_TIME_INFINITE));

  ASSERT_TRUE(on_ioctl_action.has_value());
  // Assert that CancelIoctl set the action to cancel.
  ASSERT_EQ(MLAN_ACT_CANCEL, on_ioctl_action.value());
}

TEST_F(IoctlAdapterTest, PendingIoctl) {
  // Test that a pending ioctl return code results in the ioctl adapter triggering the main process
  // which is required for the ioctl to be processed and sent to firmware.
  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status { return MLAN_STATUS_PENDING; };

  mock_mlan_.SetOnMlanIoctl(std::move(on_ioctl));

  bool trigger_main_process_called = false;
  mock_bus_.SetTriggerMainProcess([&]() {
    trigger_main_process_called = true;
    return ZX_OK;
  });

  // Perform a scan ioctl
  IoctlRequest<mlan_ds_scan> get_scan_results(MLAN_IOCTL_SCAN, MLAN_ACT_GET, 0,
                                              mlan_ds_scan{.sub_command = MLAN_OID_SCAN_NORMAL});
  ASSERT_EQ(IoctlStatus::Pending,
            ioctl_adapter_->IssueIoctl(&get_scan_results, [](pmlan_ioctl_req, IoctlStatus) {}));

  // Check that the main process was triggered
  ASSERT_TRUE(trigger_main_process_called);
}

TEST_F(IoctlAdapterTest, CancelAllIoctls) {
  // Test that the CancelAllIoctls call makes the proper call into mlan with a nullptr request.

  // Make the ioctl_req pointer point to something so we know for sure that it was nulled out.
  mlan_ioctl_req some_request;
  pmlan_ioctl_req ioctl_req = &some_request;
  sync_completion_t completion;
  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) {
    ioctl_req = req;
    sync_completion_signal(&completion);
    return MLAN_STATUS_SUCCESS;
  };
  mock_mlan_.SetOnMlanIoctl(std::move(on_ioctl));

  ioctl_adapter_->CancelAllIoctls();

  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));

  ASSERT_NULL(ioctl_req);
}

}  // namespace
