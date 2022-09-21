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

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/wlan_interface.h"

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mlan_mocks.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_bus.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_fullmac_ifc.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

using wlan::nxpfmac::WlanInterface;

namespace {

constexpr char kFullmacDeviceName[] = "test-fullmac-ifc";

struct WlanInterfaceTest : public zxtest::Test {
  void SetUp() override {
    parent_ = MockDevice::FakeRootParent();
    auto ioctl_adapter = wlan::nxpfmac::IoctlAdapter::Create(mlan_mocks_.GetAdapter(), &mock_bus_);
    ASSERT_OK(ioctl_adapter.status_value());
    ioctl_adapter_ = std::move(ioctl_adapter.value());
  }

  wlan::nxpfmac::MockBus mock_bus_;
  wlan::nxpfmac::MlanMockAdapter mlan_mocks_;
  std::unique_ptr<wlan::nxpfmac::IoctlAdapter> ioctl_adapter_;

  wlan::nxpfmac::EventHandler event_handler_;
  // This data member MUST BE LAST, because it needs to be destroyed first, ensuring that whatever
  // interface lifetimes are managed by it are destroyed before other data members.
  std::shared_ptr<MockDevice> parent_;
};

TEST_F(WlanInterfaceTest, Construct) {
  // Test that an interface can be constructed and that its lifetime is managed correctly by the
  // mock device parent. It will call release on the interface and destroy it that way.

  WlanInterface* ifc = nullptr;
  ASSERT_OK(WlanInterface::Create(parent_.get(), kFullmacDeviceName, 0, 0, nullptr, &event_handler_,
                                  nullptr, zx::channel(), &ifc));
}

TEST_F(WlanInterfaceTest, WlanFullmacImplStart) {
  // Test that WlanFullmacImplStart works and correctly passes the MLME channel back.

  zx::channel in_mlme_channel;
  zx::channel unused;
  ASSERT_OK(zx::channel::create(0, &in_mlme_channel, &unused));

  const zx_handle_t mlme_channel_handle = in_mlme_channel.get();

  WlanInterface* ifc = nullptr;
  ASSERT_OK(WlanInterface::Create(parent_.get(), kFullmacDeviceName, 0, 0, nullptr, &event_handler_,
                                  nullptr, std::move(in_mlme_channel), &ifc));

  const wlan_fullmac_impl_ifc_protocol_t fullmac_ifc{.ops = nullptr, .ctx = this};
  zx::channel out_mlme_channel;
  ifc->WlanFullmacImplStart(&fullmac_ifc, &out_mlme_channel);

  // Verify that the channel we get back from starting is the same we passed in during construction.
  // The one passed in during construction will be the one passed through wlanphy and we have to
  // pass it back here.
  ASSERT_EQ(mlme_channel_handle, out_mlme_channel.get());
}

TEST_F(WlanInterfaceTest, WlanFullmacImplQuery) {
  // Test that WlanFullmacImplQuery returns some reasonable values

  constexpr wlan_mac_role_t kRole = WLAN_MAC_ROLE_AP;
  WlanInterface* ifc = nullptr;
  ASSERT_OK(WlanInterface::Create(parent_.get(), kFullmacDeviceName, 0, kRole, nullptr,
                                  &event_handler_, nullptr, zx::channel(), &ifc));

  wlan_fullmac_query_info_t info{};
  ifc->WlanFullmacImplQuery(&info);

  // Should match the role we provided at construction
  ASSERT_EQ(kRole, info.role);

  // Should support both bands
  ASSERT_EQ(2u, info.band_cap_count);
  EXPECT_EQ(WLAN_BAND_TWO_GHZ, info.band_cap_list[0].band);
  EXPECT_EQ(WLAN_BAND_FIVE_GHZ, info.band_cap_list[1].band);

  // Should support a non-zero number of rates and channels for 2.4 GHz
  EXPECT_NE(0, info.band_cap_list[0].basic_rate_count);
  EXPECT_NE(0, info.band_cap_list[0].operating_channel_count);

  // Should support a non-zero number of rates and channels for 5 GHz
  EXPECT_NE(0, info.band_cap_list[0].basic_rate_count);
  EXPECT_NE(0, info.band_cap_list[0].operating_channel_count);
}

TEST_F(WlanInterfaceTest, WlanFullmacImplQueryMacSublayerSupport) {
  // Test that the most important values are configured in the mac sublayer support.

  WlanInterface* ifc = nullptr;
  ASSERT_OK(WlanInterface::Create(parent_.get(), kFullmacDeviceName, 0, 0, nullptr, &event_handler_,
                                  nullptr, zx::channel(), &ifc));

  mac_sublayer_support_t sublayer_support;
  ifc->WlanFullmacImplQueryMacSublayerSupport(&sublayer_support);

  // Data plan must be network device
  ASSERT_EQ(DATA_PLANE_TYPE_GENERIC_NETWORK_DEVICE, sublayer_support.data_plane.data_plane_type);
  // This is a fullmac device.
  ASSERT_EQ(MAC_IMPLEMENTATION_TYPE_FULLMAC, sublayer_support.device.mac_implementation_type);
}

TEST_F(WlanInterfaceTest, WlanFullmacImplStartScan) {
  // Test that calling start scan will eventually issue a scan IOCTL, more detailed scan tests exist
  // in the dedicated scanner tests.

  constexpr uint64_t kScanTxnId = 0x34435457;

  wlan::nxpfmac::MockFullmacIfc mock_fullmac_ifc;

  mlan_mocks_.SetOnMlanIoctl([&](t_void*, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_SET && req->req_id == MLAN_IOCTL_SCAN) {
      // Start scan, has to be completed asynchronously.
      ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
      return MLAN_STATUS_PENDING;
    }
    if (req->action == MLAN_ACT_GET && req->req_id == MLAN_IOCTL_SCAN) {
      // Get scan results
      auto scan_req = reinterpret_cast<mlan_ds_scan*>(req->pbuf);
      // Make it easy for ourselves by saying there are no scan results.
      scan_req->param.scan_resp.num_in_scan_table = 0;
      return MLAN_STATUS_SUCCESS;
    }
    // Return success for everything else.
    return MLAN_STATUS_SUCCESS;
  });

  zx::channel in_mlme_channel;
  zx::channel unused;
  ASSERT_OK(zx::channel::create(0, &in_mlme_channel, &unused));

  WlanInterface* ifc = nullptr;
  ASSERT_OK(WlanInterface::Create(parent_.get(), kFullmacDeviceName, 0, 0, nullptr, &event_handler_,
                                  ioctl_adapter_.get(), std::move(in_mlme_channel), &ifc));

  zx::channel out_mlme_channel;
  ifc->WlanFullmacImplStart(mock_fullmac_ifc.proto(), &out_mlme_channel);

  const wlan_fullmac_scan_req_t scan_request{
      .txn_id = kScanTxnId,
      .scan_type = WLAN_SCAN_TYPE_ACTIVE,
  };
  ifc->WlanFullmacImplStartScan(&scan_request);

  // Because there are no scan results we just expect a scan end. We really only need to verify that
  // the scanner was called as a result of our StartScan call.
  sync_completion_t scan_end_called;
  mock_fullmac_ifc.on_scan_end.ExpectCallWithMatcher([&](const wlan_fullmac_scan_end_t* scan_end) {
    EXPECT_EQ(kScanTxnId, scan_end->txn_id);
    sync_completion_signal(&scan_end_called);
  });

  mlan_event scan_report_event{.event_id = MLAN_EVENT_ID_DRV_SCAN_REPORT};

  // Send a report indicating there's a scan report.
  event_handler_.OnEvent(&scan_report_event);

  ASSERT_OK(sync_completion_wait(&scan_end_called, ZX_TIME_INFINITE));

  mock_fullmac_ifc.on_scan_end.VerifyAndClear();
}

TEST_F(WlanInterfaceTest, WlanFullmacImplConnectReq) {
  wlan::nxpfmac::MockFullmacIfc mock_fullmac_ifc;

  mlan_mocks_.SetOnMlanIoctl([&](t_void*, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_SET && req->req_id == MLAN_IOCTL_BSS) {
      // Connect request, must complete asynchronously.
      ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
      return MLAN_STATUS_PENDING;
    }
    // Return success for everything else.
    return MLAN_STATUS_SUCCESS;
  });

  zx::channel in_mlme_channel;
  zx::channel unused;
  ASSERT_OK(zx::channel::create(0, &in_mlme_channel, &unused));

  WlanInterface* ifc = nullptr;
  ASSERT_OK(WlanInterface::Create(parent_.get(), kFullmacDeviceName, 0, 0, nullptr, &event_handler_,
                                  ioctl_adapter_.get(), std::move(in_mlme_channel), &ifc));

  zx::channel out_mlme_channel;
  ifc->WlanFullmacImplStart(mock_fullmac_ifc.proto(), &out_mlme_channel);

  sync_completion_t on_connect_conf_called;
  mock_fullmac_ifc.on_connect_conf.ExpectCallWithMatcher(
      [&](const wlan_fullmac_connect_confirm_t* resp) {
        sync_completion_signal(&on_connect_conf_called);
      });

  const wlan_fullmac_connect_req_t connect_request{

  };
  ifc->WlanFullmacImplConnectReq(&connect_request);

  ASSERT_OK(sync_completion_wait(&on_connect_conf_called, ZX_TIME_INFINITE));
}

}  // namespace
