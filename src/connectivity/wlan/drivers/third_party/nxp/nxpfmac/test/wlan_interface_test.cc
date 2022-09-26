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

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/data_plane.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device_context.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mlan_mocks.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_bus.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_fullmac_ifc.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/test_data_plane.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

using wlan::nxpfmac::WlanInterface;

namespace {

constexpr char kFullmacDeviceName[] = "test-fullmac-ifc";

struct WlanInterfaceTest : public zxtest::Test, public wlan::nxpfmac::DataPlaneIfc {
  void SetUp() override {
    parent_ = MockDevice::FakeRootParent();
    ASSERT_OK(wlan::nxpfmac::TestDataPlane::Create(this, &mock_bus_, mlan_mocks_.GetAdapter(),
                                                   &test_data_plane_));

    // The last device added right after creating the dataplane should be the network device.
    SetupNetDevice(test_data_plane_->GetNetDevice());

    auto ioctl_adapter = wlan::nxpfmac::IoctlAdapter::Create(mlan_mocks_.GetAdapter(), &mock_bus_);
    ASSERT_OK(ioctl_adapter.status_value());
    ioctl_adapter_ = std::move(ioctl_adapter.value());
    context_.event_handler_ = &event_handler_;
    context_.ioctl_adapter_ = ioctl_adapter_.get();
    context_.data_plane_ = test_data_plane_->GetDataPlane();
  }

  void TearDown() override {
    // Destroy the dataplane before the mock device. This ensures a safe destruction before the
    // parent device of the NetworkDeviceImpl device goes away.
    test_data_plane_.reset();
  }

  void SetupNetDevice(zx_device* net_device) {
    // Because WlanInterface creates a port for the netdevice we need to provide a limited
    // implementation of the netdevice ifc.
    network_device_impl_protocol_t netdev_proto;
    ASSERT_OK(device_get_protocol(net_device, ZX_PROTOCOL_NETWORK_DEVICE_IMPL, &netdev_proto));
    ASSERT_OK(
        network_device_impl_init(&netdev_proto, netdev_ifc_proto_.ctx, netdev_ifc_proto_.ops));
  }

  static void OnAddPort(void* ctx, uint8_t, const network_port_protocol_t* proto) {
    auto ifc = static_cast<WlanInterfaceTest*>(ctx);
    ifc->net_port_proto_ = *proto;
    EXPECT_NOT_NULL(proto->ctx);
    EXPECT_NOT_NULL(proto->ops);
    sync_completion_signal(&ifc->on_add_port_called_);
  }
  static void OnRemovePort(void* ctx, uint8_t) {
    auto ifc = static_cast<WlanInterfaceTest*>(ctx);
    sync_completion_signal(&ifc->on_remove_port_called_);
    if (ifc->net_port_proto_.ctx && ifc->net_port_proto_.ops) {
      network_port_removed(&ifc->net_port_proto_);
    }
  }

  // DataPlaneIfc implementation
  void OnEapolTransmitted(wlan::drivers::components::Frame&& frame, zx_status_t status) override {
    sync_completion_signal(&on_eapol_transmitted_called_);
  }
  void OnEapolReceived(wlan::drivers::components::Frame&& frame) override {
    sync_completion_signal(&on_eapol_received_called_);
  }

  network_device_ifc_protocol_ops_t netdev_ifc_proto_ops_{.add_port = &OnAddPort,
                                                          .remove_port = &OnRemovePort};
  network_device_ifc_protocol_t netdev_ifc_proto_{.ops = &netdev_ifc_proto_ops_, .ctx = this};

  network_port_protocol_t net_port_proto_;
  sync_completion_t on_add_port_called_;
  sync_completion_t on_remove_port_called_;
  sync_completion_t on_eapol_transmitted_called_;
  sync_completion_t on_eapol_received_called_;

  wlan::nxpfmac::EventHandler event_handler_;
  wlan::nxpfmac::MockBus mock_bus_;
  wlan::nxpfmac::MlanMockAdapter mlan_mocks_;
  std::unique_ptr<wlan::nxpfmac::IoctlAdapter> ioctl_adapter_;
  std::unique_ptr<wlan::nxpfmac::TestDataPlane> test_data_plane_;
  wlan::nxpfmac::DeviceContext context_;
  // This data member MUST BE LAST, because it needs to be destroyed first, ensuring that whatever
  // interface lifetimes are managed by it are destroyed before other data members.
  std::shared_ptr<MockDevice> parent_;
};

TEST_F(WlanInterfaceTest, Construct) {
  // Test that an interface can be constructed and that its lifetime is managed correctly by the
  // mock device parent. It will call release on the interface and destroy it that way.

  WlanInterface* ifc = nullptr;
  ASSERT_OK(WlanInterface::Create(parent_.get(), kFullmacDeviceName, 0, WLAN_MAC_ROLE_CLIENT,
                                  &context_, zx::channel(), &ifc));
}

TEST_F(WlanInterfaceTest, WlanFullmacImplStart) {
  // Test that WlanFullmacImplStart works and correctly passes the MLME channel back.

  zx::channel in_mlme_channel;
  zx::channel unused;
  ASSERT_OK(zx::channel::create(0, &in_mlme_channel, &unused));

  const zx_handle_t mlme_channel_handle = in_mlme_channel.get();

  WlanInterface* ifc = nullptr;
  ASSERT_OK(WlanInterface::Create(parent_.get(), kFullmacDeviceName, 0, WLAN_MAC_ROLE_CLIENT,
                                  &context_, std::move(in_mlme_channel), &ifc));

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
  ASSERT_OK(WlanInterface::Create(parent_.get(), kFullmacDeviceName, 0, kRole, &context_,
                                  zx::channel(), &ifc));

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
  ASSERT_OK(WlanInterface::Create(parent_.get(), kFullmacDeviceName, 0, WLAN_MAC_ROLE_CLIENT,
                                  &context_, zx::channel(), &ifc));

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
  ASSERT_OK(WlanInterface::Create(parent_.get(), kFullmacDeviceName, 0, WLAN_MAC_ROLE_CLIENT,
                                  &context_, std::move(in_mlme_channel), &ifc));

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
  // Test that a connect request issues the correct connect request. More detailed tests exist in
  // the dedicated ClientConnection tests.

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
  ASSERT_OK(WlanInterface::Create(parent_.get(), kFullmacDeviceName, 0, WLAN_MAC_ROLE_CLIENT,
                                  &context_, std::move(in_mlme_channel), &ifc));

  zx::channel out_mlme_channel;
  ifc->WlanFullmacImplStart(mock_fullmac_ifc.proto(), &out_mlme_channel);

  sync_completion_t on_connect_conf_called;
  mock_fullmac_ifc.on_connect_conf.ExpectCallWithMatcher(
      [&](const wlan_fullmac_connect_confirm_t* resp) {
        sync_completion_signal(&on_connect_conf_called);
      });

  constexpr uint8_t kIesWithSsid[] = {"\x00\x04Test"};
  constexpr uint8_t kTestChannel = 1;
  const wlan_fullmac_connect_req_t connect_request = {
      .selected_bss{.ies_list = kIesWithSsid,
                    .ies_count = sizeof(kIesWithSsid),
                    .channel{.primary = kTestChannel}},
      .auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM};

  ifc->WlanFullmacImplConnectReq(&connect_request);

  ASSERT_OK(sync_completion_wait(&on_connect_conf_called, ZX_TIME_INFINITE));
}

TEST_F(WlanInterfaceTest, MacSetMode) {
  // Test that MacSetMode actually sets the mac mode.

  wlan::nxpfmac::MockFullmacIfc mock_fullmac_ifc;

  constexpr uint8_t kMacMulticastFilters[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                              0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};

  std::atomic<mode_t> mac_mode;

  mlan_mocks_.SetOnMlanIoctl([&](t_void*, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_SET && req->req_id == MLAN_IOCTL_BSS) {
      auto bss = reinterpret_cast<const mlan_ds_bss*>(req->pbuf);
      EXPECT_EQ(MLAN_OID_BSS_MULTICAST_LIST, bss->sub_command);
      switch (mac_mode.load()) {
        case MODE_MULTICAST_FILTER:
          EXPECT_EQ(MLAN_MULTICAST_MODE, bss->param.multicast_list.mode);
          EXPECT_EQ(sizeof(kMacMulticastFilters) / ETH_ALEN,
                    bss->param.multicast_list.num_multicast_addr);
          EXPECT_BYTES_EQ(kMacMulticastFilters, bss->param.multicast_list.mac_list,
                          sizeof(kMacMulticastFilters));
          break;
        case MODE_MULTICAST_PROMISCUOUS:
          EXPECT_EQ(MLAN_ALL_MULTI_MODE, bss->param.multicast_list.mode);
          EXPECT_EQ(0, bss->param.multicast_list.num_multicast_addr);
          break;
        case MODE_PROMISCUOUS:
          EXPECT_EQ(MLAN_PROMISC_MODE, bss->param.multicast_list.mode);
          EXPECT_EQ(0, bss->param.multicast_list.num_multicast_addr);
          break;
        default:
          ADD_FAILURE("Unexpected mac mode: %u", mac_mode.load());
          break;
      }
      return MLAN_STATUS_SUCCESS;
    }
    // Return success for everything else.
    return MLAN_STATUS_SUCCESS;
  });

  zx::channel in_mlme_channel;
  zx::channel unused;
  ASSERT_OK(zx::channel::create(0, &in_mlme_channel, &unused));

  WlanInterface* ifc = nullptr;
  ASSERT_OK(WlanInterface::Create(parent_.get(), kFullmacDeviceName, 0, WLAN_MAC_ROLE_CLIENT,
                                  &context_, std::move(in_mlme_channel), &ifc));

  constexpr mode_t kMacModes[] = {MODE_MULTICAST_FILTER, MODE_MULTICAST_PROMISCUOUS,
                                  MODE_PROMISCUOUS};
  for (auto mode : kMacModes) {
    mac_mode = mode;
    ifc->MacSetMode(mode, kMacMulticastFilters);
  }
}

}  // namespace
