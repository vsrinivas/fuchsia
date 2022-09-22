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

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/scanner.h"

#include <lib/sync/completion.h>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device_context.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/event_handler.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mlan_mocks.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_bus.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_fullmac_ifc.h"

namespace {

using wlan::nxpfmac::Scanner;

struct ScannerTest : public zxtest::Test {
  void SetUp() override {
    auto ioctl_adapter = wlan::nxpfmac::IoctlAdapter::Create(mocks_.GetAdapter(), &mock_bus_);
    ASSERT_OK(ioctl_adapter.status_value());
    ioctl_adapter_ = std::move(ioctl_adapter.value());
    mock_fullmac_ifc_client_ = ddk::WlanFullmacImplIfcProtocolClient(mock_fullmac_ifc_.proto());
    context_.ioctl_adapter_ = ioctl_adapter_.get();
    context_.event_handler_ = &event_handler_;
  }

  wlan::nxpfmac::DeviceContext context_;
  wlan::nxpfmac::MlanMockAdapter mocks_;
  wlan::nxpfmac::EventHandler event_handler_;
  wlan::nxpfmac::MockBus mock_bus_;
  wlan::nxpfmac::MockFullmacIfc mock_fullmac_ifc_;
  ddk::WlanFullmacImplIfcProtocolClient mock_fullmac_ifc_client_;
  std::unique_ptr<wlan::nxpfmac::IoctlAdapter> ioctl_adapter_;
};

// Make sure this timeout is long enough to avoid flaky tests for all tests that are not expected to
// time out.
constexpr zx_duration_t kDefaultTimeout = ZX_MSEC(60000000000);

TEST_F(ScannerTest, Constructible) {
  // Test that a Scanner object can be constructed without crashing.

  wlan::nxpfmac::EventHandler event_handler;
  ASSERT_NO_FATAL_FAILURE(Scanner(nullptr, &context_, 0));
}

TEST_F(ScannerTest, Scan) {
  // Test a basic scan and ensure that the correct scan results are passed on to the fullmac ifc.

  constexpr uint32_t kBssIndex = 0;
  constexpr uint64_t kTxnId = 0x234776898adf83;

  // Our scan result
  BSSDescriptor_t scan_table[] = {{
      .mac_address = {0x01, 0x02, 0x03, 0x04, 0x5, 0x06},
      .rssi = -40,
      .channel = 11,
      .beacon_period = 100,
      .curr_bandwidth = 6,
  }};

  bool on_ioctl_set_called = false;
  bool on_ioctl_get_called = false;
  auto on_ioctl = [&](t_void*, pmlan_ioctl_req req) -> mlan_status {
    auto request = reinterpret_cast<wlan::nxpfmac::IoctlRequest<mlan_ds_scan>*>(req);
    if (req->action == MLAN_ACT_SET) {
      on_ioctl_set_called = true;
      ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
      return MLAN_STATUS_PENDING;
    }
    if (req->action == MLAN_ACT_GET) {
      request->UserReq().param.scan_resp.num_in_scan_table = std::size(scan_table);
      request->UserReq().param.scan_resp.pscan_table = reinterpret_cast<uint8_t*>(&scan_table);

      on_ioctl_get_called = true;
      return MLAN_STATUS_SUCCESS;
    }
    if (req->action == MLAN_ACT_CANCEL) {
      return MLAN_STATUS_SUCCESS;
    }
    return MLAN_STATUS_FAILURE;
  };

  mocks_.SetOnMlanIoctl(std::move(on_ioctl));

  sync_completion_t scan_result_completion;
  sync_completion_t scan_end_completion;

  std::atomic<int> scan_results_seen = 0;
  mock_fullmac_ifc_.on_scan_result.ExpectCallWithMatcher(
      [&](const wlan_fullmac_scan_result_t* result) {
        ++scan_results_seen;
        EXPECT_BYTES_EQ(result->bss.bssid, scan_table[0].mac_address, sizeof(result->bss.bssid));
        sync_completion_signal(&scan_result_completion);
      });

  mock_fullmac_ifc_.on_scan_end.ExpectCallWithMatcher([&](const wlan_fullmac_scan_end_t* end) {
    EXPECT_EQ(kTxnId, end->txn_id);
    EXPECT_EQ(WLAN_SCAN_RESULT_SUCCESS, end->code);
    sync_completion_signal(&scan_end_completion);
  });

  Scanner scanner(&mock_fullmac_ifc_client_, &context_, kBssIndex);

  // Perform an active scan
  wlan_fullmac_scan_req_t scan_request{
      .txn_id = kTxnId,
      .scan_type = WLAN_SCAN_TYPE_ACTIVE,
  };
  ASSERT_OK(scanner.Scan(&scan_request, kDefaultTimeout));

  // The Set ioctl should've been called immediately, that's what starts the scan
  ASSERT_TRUE(on_ioctl_set_called);

  // Send an event indicating that there is a scan report. This triggers the retrieval of scan
  // results.
  mlan_event event{.event_id = MLAN_EVENT_ID_DRV_SCAN_REPORT};
  event_handler_.OnEvent(&event);

  // Verify that scan results were recieved and that scan end was called.
  ASSERT_OK(sync_completion_wait(&scan_result_completion, ZX_TIME_INFINITE));
  ASSERT_OK(sync_completion_wait(&scan_end_completion, ZX_TIME_INFINITE));

  // The Get ioctl should be called as part of getting the scan results.
  ASSERT_TRUE(on_ioctl_get_called);

  ASSERT_EQ(std::size(scan_table), scan_results_seen.load());

  // At this point there should be no scan to stop.
  ASSERT_EQ(ZX_ERR_NOT_FOUND, scanner.StopScan());
}

TEST_F(ScannerTest, StopScan) {
  // Test that StopScan works on a pending scan, test that StopScan returns an error if no scan in
  // progress and test that we can scan after stopping a scan.

  constexpr uint32_t kBssIndex = 0;
  constexpr uint64_t kTxnId = 0x234776898adf83;

  sync_completion_t called_with_cancel;
  auto on_ioctl = [&](t_void*, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_CANCEL) {
      sync_completion_signal(&called_with_cancel);
      // The cancelation code has to set the status to canceled so that IoctlAdapter can figure out
      // that it's canceled successfully.
      req->status_code = MLAN_ERROR_CMD_CANCEL;
      // The cancelation also needs to call ioctl complete with a failure status.
      ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Failure);
      return MLAN_STATUS_SUCCESS;
    }
    // Don't call the callback here, let the scan languish so we can stop it.
    return MLAN_STATUS_PENDING;
  };

  mocks_.SetOnMlanIoctl(std::move(on_ioctl));

  sync_completion_t scan_ended;
  mock_fullmac_ifc_.on_scan_end.ExpectCallWithMatcher([&](const wlan_fullmac_scan_end_t* end) {
    EXPECT_EQ(kTxnId, end->txn_id);
    EXPECT_EQ(WLAN_SCAN_RESULT_CANCELED_BY_DRIVER_OR_FIRMWARE, end->code);
    sync_completion_signal(&scan_ended);
  });

  {
    Scanner scanner(&mock_fullmac_ifc_client_, &context_, kBssIndex);

    // Perform an active scan
    wlan_fullmac_scan_req_t scan_request{.txn_id = kTxnId, .scan_type = WLAN_SCAN_TYPE_ACTIVE};

    ASSERT_OK(scanner.Scan(&scan_request, kDefaultTimeout));

    // Verify that we can stop a scan in progress
    ASSERT_OK(scanner.StopScan());

    // Wait for the ioctl to be called with the cancel.
    ASSERT_OK(sync_completion_wait(&called_with_cancel, ZX_TIME_INFINITE));

    // Wait for the scan end to be signaled to the fullmac ifc
    ASSERT_OK(sync_completion_wait(&scan_ended, ZX_TIME_INFINITE));

    mock_fullmac_ifc_.on_scan_end.VerifyAndClear();

    // Verify that if there is no scan in progress StopScan should return an error.
    ASSERT_EQ(ZX_ERR_NOT_FOUND, scanner.StopScan());

    sync_completion_reset(&scan_ended);
    // The second scan should be canceled when the scanner is destroyed
    mock_fullmac_ifc_.on_scan_end.ExpectCallWithMatcher([&](const wlan_fullmac_scan_end_t* end) {
      EXPECT_EQ(kTxnId, end->txn_id);
      EXPECT_EQ(WLAN_SCAN_RESULT_CANCELED_BY_DRIVER_OR_FIRMWARE, end->code);
      sync_completion_signal(&scan_ended);
    });

    // Verify that we can scan again after stopping.
    ASSERT_OK(scanner.Scan(&scan_request, kDefaultTimeout));
  }
  // Wait for the second scan end to be signaled to the fullmac ifc
  ASSERT_OK(sync_completion_wait(&scan_ended, ZX_TIME_INFINITE));

  mock_fullmac_ifc_.on_scan_end.VerifyAndClear();
}

TEST_F(ScannerTest, StopScanWithNoScanInProgress) {
  // Test that StopScan returns an error when no scan is in progress.

  constexpr uint32_t kBssIndex = 0;
  Scanner scanner(nullptr, &context_, kBssIndex);

  ASSERT_EQ(ZX_ERR_NOT_FOUND, scanner.StopScan());
}

TEST_F(ScannerTest, ScanSpecificSsid) {
  // Test scanning of a specific SSID

  constexpr uint32_t kBssIndex = 0;
  constexpr uint64_t kTxnId = 42;

  wlan_fullmac_scan_req_t scan_request{.txn_id = kTxnId, .scan_type = WLAN_SCAN_TYPE_ACTIVE};

  constexpr cssid_t kSsid{.len = 4, .data{"foo"}};

  scan_request.ssids_list = &kSsid;
  scan_request.ssids_count = 1u;

  auto on_ioctl = [&](t_void*, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_GET) {
      // Don't do anything with the request to get scan results.
      return MLAN_STATUS_SUCCESS;
    }
    EXPECT_EQ(MLAN_ACT_SET, req->action);
    EXPECT_EQ(MLAN_IOCTL_SCAN, req->req_id);
    auto scan = reinterpret_cast<mlan_ds_scan*>(req->pbuf);
    EXPECT_EQ(MLAN_OID_SCAN_NORMAL, scan->sub_command);
    auto& scan_req = scan->param.scan_req;

    // Check that the requested SSID is part of the request.
    EXPECT_BYTES_EQ(kSsid.data, scan_req.scan_ssid.ssid, kSsid.len);
    ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
    return MLAN_STATUS_PENDING;
  };

  mocks_.SetOnMlanIoctl(std::move(on_ioctl));

  sync_completion_t completion;
  mock_fullmac_ifc_.on_scan_end.ExpectCallWithMatcher([&](const wlan_fullmac_scan_end_t* end) {
    EXPECT_EQ(WLAN_SCAN_RESULT_SUCCESS, end->code);
    EXPECT_EQ(kTxnId, end->txn_id);
    sync_completion_signal(&completion);
  });

  Scanner scanner(&mock_fullmac_ifc_client_, &context_, kBssIndex);

  ASSERT_OK(scanner.Scan(&scan_request, kDefaultTimeout));

  // End the scan by sending a scan report event.
  mlan_event event{.event_id = MLAN_EVENT_ID_DRV_SCAN_REPORT};
  event_handler_.OnEvent(&event);

  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));
}

TEST_F(ScannerTest, ScanTooManySsids) {
  // Test that scanning more than the supported number of SSIDs in one request will fail.

  constexpr uint32_t kBssIndex = 0;
  constexpr uint64_t kTxnId = 42;

  wlan_fullmac_scan_req_t scan_request{.txn_id = kTxnId, .scan_type = WLAN_SCAN_TYPE_ACTIVE};

  constexpr cssid_t kSsids[] = {{.len = 4, .data{"foo"}}, {.len = 3, .data{"ap"}}};

  scan_request.ssids_list = kSsids;
  scan_request.ssids_count = std::size(kSsids);
  ASSERT_GT(scan_request.ssids_count, 1u);

  Scanner scanner(&mock_fullmac_ifc_client_, &context_, kBssIndex);

  // This should immediately fail with an invalid args error.
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, scanner.Scan(&scan_request, kDefaultTimeout));
}

TEST_F(ScannerTest, ScanTimeout) {
  // Test that a scan times out properly.

  constexpr uint32_t kBssIndex = 0;
  constexpr uint64_t kTxnId = 42;
  constexpr zx_duration_t kShortTimeout = ZX_MSEC(5);

  wlan_fullmac_scan_req_t scan_request{.txn_id = kTxnId, .scan_type = WLAN_SCAN_TYPE_ACTIVE};

  std::atomic<int> on_ioctl_calls = 0;
  std::atomic<bool> get_scan_results_called = false;
  auto on_ioctl = [&](t_void*, pmlan_ioctl_req req) -> mlan_status {
    ++on_ioctl_calls;
    if (req->action == MLAN_ACT_CANCEL) {
      // The timeout will trigger a cancelation, we must emulate mlan's behavior of calling the
      // ioctl complete with a specific status code and result in this case.
      req->status_code = MLAN_ERROR_CMD_CANCEL;
      ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Failure);
      return MLAN_STATUS_SUCCESS;
    }
    if (req->action == MLAN_ACT_GET) {
      // The scanner will attempt to get any partial scan results available. This operation should
      // immediately return success.
      get_scan_results_called = true;
      return MLAN_STATUS_SUCCESS;
    }
    // Leave the scan as pending, allowing it to time out.
    return MLAN_STATUS_PENDING;
  };

  mocks_.SetOnMlanIoctl(std::move(on_ioctl));

  sync_completion_t completion;
  mock_fullmac_ifc_.on_scan_end.ExpectCallWithMatcher([&](const wlan_fullmac_scan_end_t* end) {
    EXPECT_EQ(WLAN_SCAN_RESULT_CANCELED_BY_DRIVER_OR_FIRMWARE, end->code);
    EXPECT_EQ(kTxnId, end->txn_id);
    sync_completion_signal(&completion);
  });

  Scanner scanner(&mock_fullmac_ifc_client_, &context_, kBssIndex);

  ASSERT_OK(scanner.Scan(&scan_request, kShortTimeout));

  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));
  // There should only have been three ioctl calls. One to start the scan, one to cancel it, and one
  // to fetch scan results.
  ASSERT_EQ(3u, on_ioctl_calls.load());
  ASSERT_TRUE(get_scan_results_called.load());
}

}  // namespace
