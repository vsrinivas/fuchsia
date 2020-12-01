// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/internal/cpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl/cpp/decoder.h>
#include <lib/fidl/cpp/message.h>

#include <functional>
#include <memory>
#include <optional>
#include <tuple>

#include <gtest/gtest.h>
#include <src/connectivity/wlan/drivers/wlanif/device.h>
#include <wlan/mlme/dispatcher.h>

namespace wlan_internal = ::fuchsia::wlan::internal;
namespace wlan_mlme = ::fuchsia::wlan::mlme;

bool multicast_promisc_enabled = false;

static zx_status_t hook_set_multicast_promisc(void* ctx, bool enable) {
  multicast_promisc_enabled = enable;
  return ZX_OK;
}

std::pair<zx::channel, zx::channel> make_channel() {
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  return {std::move(local), std::move(remote)};
}

bool timeout_after(zx_duration_t duration, const std::function<bool()>& predicate) {
  while (!predicate()) {
    zx_duration_t sleep = ZX_MSEC(100);
    zx_nanosleep(zx_deadline_after(sleep));
    if (duration <= 0) {
      return false;
    }
    duration -= sleep;
    if (!timeout_after(duration, predicate)) {
      return false;
    }
  }
  return true;
}

// Verify that receiving an ethernet SetParam for multicast promiscuous mode results in a call to
// wlanif_impl->set_muilticast_promisc.
TEST(MulticastPromiscMode, OnOff) {
  zx_status_t status;

  wlanif_impl_protocol_ops_t proto_ops = {.set_multicast_promisc = hook_set_multicast_promisc};
  wlanif_impl_protocol_t proto = {.ops = &proto_ops};
  wlanif::Device device(nullptr, proto);

  multicast_promisc_enabled = false;

  // Disable => Enable
  status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 1, nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(multicast_promisc_enabled, true);

  // Enable => Enable
  status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 1, nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(multicast_promisc_enabled, true);

  // Enable => Enable (any non-zero value should be treated as "true")
  status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 0x80, nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(multicast_promisc_enabled, true);

  // Enable => Disable
  status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 0, nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(multicast_promisc_enabled, false);
}

// Verify that we get a ZX_ERR_UNSUPPORTED back if the set_multicast_promisc hook is unimplemented.
TEST(MulticastPromiscMode, Unimplemented) {
  zx_status_t status;

  wlanif_impl_protocol_ops_t proto_ops = {};
  wlanif_impl_protocol_t proto = {.ops = &proto_ops};
  wlanif::Device device(nullptr, proto);

  multicast_promisc_enabled = false;

  status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 1, nullptr, 0);
  EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(multicast_promisc_enabled, false);
}

struct SmeChannelTestContext {
  SmeChannelTestContext() {
    auto [new_sme, new_mlme] = make_channel();
    mlme = std::move(new_mlme);
    sme = std::move(new_sme);
  }

  zx::channel mlme = {};
  zx::channel sme = {};
  std::optional<wlanif_scan_req_t> scan_req = {};
};

wlanif_impl_protocol_ops_t EmptyProtoOps() {
  return wlanif_impl_protocol_ops_t{
      // Each instance is required to provide its own .start() method to store the MLME channels.
      // SME Channel will be provided to wlanif-impl-driver when it calls back into its parent.
      .query = [](void* ctx, wlanif_query_info_t* info) {},
      .start_scan = [](void* ctx, const wlanif_scan_req_t* req) {},
      .join_req = [](void* ctx, const wlanif_join_req_t* req) {},
      .auth_req = [](void* ctx, const wlanif_auth_req_t* req) {},
      .auth_resp = [](void* ctx, const wlanif_auth_resp_t* req) {},
      .deauth_req = [](void* ctx, const wlanif_deauth_req_t* req) {},
      .assoc_req = [](void* ctx, const wlanif_assoc_req_t* req) {},
      .assoc_resp = [](void* ctx, const wlanif_assoc_resp_t* req) {},
      .disassoc_req = [](void* ctx, const wlanif_disassoc_req_t* req) {},
      .reset_req = [](void* ctx, const wlanif_reset_req_t* req) {},
      .start_req = [](void* ctx, const wlanif_start_req_t* req) {},
      .stop_req = [](void* ctx, const wlanif_stop_req_t* req) {},
      .set_keys_req = [](void* ctx, const wlanif_set_keys_req_t* req) {},
      .del_keys_req = [](void* ctx, const wlanif_del_keys_req_t* req) {},
      .eapol_req = [](void* ctx, const wlanif_eapol_req_t* req) {},
  };
}

TEST(SmeChannel, Bound) {
#define SME_DEV(c) static_cast<SmeChannelTestContext*>(c)
  wlanif_impl_protocol_ops_t proto_ops = EmptyProtoOps();
  proto_ops.start = [](void* ctx, const wlanif_impl_ifc_protocol_t* ifc,
                       zx_handle_t* out_sme_channel) -> zx_status_t {
    *out_sme_channel = SME_DEV(ctx)->sme.release();
    return ZX_OK;
  };

  // Capture incoming scan request.
  proto_ops.start_scan = [](void* ctx, const wlanif_scan_req_t* req) {
    SME_DEV(ctx)->scan_req = {{
        .bss_type = req->bss_type,
        .scan_type = req->scan_type,
    }};
  };

  SmeChannelTestContext ctx;
  wlanif_impl_protocol_t proto = {
      .ops = &proto_ops,
      .ctx = &ctx,
  };

  fake_ddk::Bind ddk;
  // Unsafe cast, however, parent is never used as fake_ddk replaces default device manager.
  auto parent = reinterpret_cast<zx_device_t*>(&ctx);
  auto device = wlanif::Device{parent, proto};
  auto status = device.Bind();
  ASSERT_EQ(status, ZX_OK);

  // Send scan request to device.
  auto mlme_proxy = wlan_mlme::MLME_SyncProxy(std::move(ctx.mlme));
  mlme_proxy.StartScan(wlan_mlme::ScanRequest{
      .bss_type = wlan_internal::BssTypes::INFRASTRUCTURE,
      .scan_type = wlan_mlme::ScanTypes::PASSIVE,
  });

  // Wait for scan message to propagate through the system.
  ASSERT_TRUE(timeout_after(ZX_SEC(120), [&]() { return ctx.scan_req.has_value(); }));

  // Verify scan request.
  ASSERT_TRUE(ctx.scan_req.has_value());
  ASSERT_EQ(ctx.scan_req->bss_type, WLAN_BSS_TYPE_INFRASTRUCTURE);
  ASSERT_EQ(ctx.scan_req->scan_type, WLAN_SCAN_TYPE_PASSIVE);

  device.EthUnbind();
}

// Tests that the device will be unbound following a failed device bind.
TEST(SmeChannel, FailedBind) {
  wlanif_impl_protocol_ops_t proto_ops = EmptyProtoOps();
  proto_ops.start = [](void* ctx, const wlanif_impl_ifc_protocol_t* ifc,
                       zx_handle_t* out_sme_channel) -> zx_status_t {
    *out_sme_channel = static_cast<SmeChannelTestContext*>(ctx)->sme.release();
    return ZX_OK;
  };

  SmeChannelTestContext ctx;
  wlanif_impl_protocol_t proto = {
      .ops = &proto_ops,
      .ctx = &ctx,
  };

  fake_ddk::Bind ddk;
  auto device = wlanif::Device{fake_ddk::kFakeParent, proto};

  // Connect a mock channel so that the next device bind will fail.
  zx::channel local, remote;
  ASSERT_EQ(zx::channel::create(0, &local, &remote), ZX_OK);
  ASSERT_EQ(device.Connect(std::move(remote)), ZX_OK);

  // This should fail and request the device be unbound.
  auto status = device.Bind();
  ASSERT_NE(status, ZX_OK);
  ASSERT_EQ(ddk.WaitUntilRemove(), ZX_OK);
  ASSERT_TRUE(ddk.Ok());
}

struct AssocReqTestContext {
  AssocReqTestContext() {
    auto [new_sme, new_mlme] = make_channel();
    mlme = std::move(new_mlme);
    sme = std::move(new_sme);
  }

  zx::channel mlme = {};
  zx::channel sme = {};
  std::optional<wlanif_assoc_req_t> assoc_req = {};
  wlanif_impl_ifc_protocol_t ifc = {};
  volatile std::atomic<bool> assoc_received = false;
  volatile std::atomic<bool> assoc_confirmed = false;
  volatile std::atomic<bool> ignore_assoc = false;
};

TEST(AssocReqHandling, MultipleAssocReq) {
#define ASSOC_DEV(c) static_cast<AssocReqTestContext*>(c)
  static wlanif_impl_ifc_protocol_ops_t wlanif_impl_ifc_ops = {
      // MLME operations
      .assoc_conf =
          [](void* cookie, const wlanif_assoc_confirm_t* resp) {
            ASSOC_DEV(cookie)->assoc_confirmed = true;
          },
  };

  wlanif_impl_protocol_ops_t proto_ops = EmptyProtoOps();
  proto_ops.start = [](void* ctx, const wlanif_impl_ifc_protocol_t* ifc,
                       zx_handle_t* out_sme_channel) -> zx_status_t {
    *out_sme_channel = ASSOC_DEV(ctx)->sme.release();
    // Substitute with our own ops to capture assoc conf
    ASSOC_DEV(ctx)->ifc.ops = &wlanif_impl_ifc_ops;
    ASSOC_DEV(ctx)->ifc.ctx = ctx;
    return ZX_OK;
  };

  proto_ops.assoc_req = [](void* ctx, const wlanif_assoc_req_t* req) {
    if (ASSOC_DEV(ctx)->ignore_assoc == false) {
      ASSOC_DEV(ctx)->assoc_req = {{
          .rsne_len = req->rsne_len,
          .vendor_ie_len = req->vendor_ie_len,
      }};
      wlanif_assoc_confirm_t conf;

      conf.result_code = 0;
      conf.association_id = 1;
      wlanif_impl_ifc_assoc_conf(&ASSOC_DEV(ctx)->ifc, &conf);
    }
    ASSOC_DEV(ctx)->assoc_received = true;
  };
  AssocReqTestContext ctx;
  wlanif_impl_protocol_t proto = {
      .ops = &proto_ops,
      .ctx = &ctx,
  };

  fake_ddk::Bind ddk;
  // Unsafe cast, however, parent is never used as fake_ddk replaces default device manager.
  auto parent = reinterpret_cast<zx_device_t*>(&ctx);
  auto device = wlanif::Device{parent, proto};
  auto status = device.Bind();
  ASSERT_EQ(status, ZX_OK);

  // Send assoc request to device, ignore this one.
  ctx.ignore_assoc = true;
  auto mlme_proxy = wlan_mlme::MLME_SyncProxy(std::move(ctx.mlme));
  mlme_proxy.AssociateReq(wlan_mlme::AssociateRequest{
      .rsne = {},
      .vendor_ies = {},
  });

  // Wait for assoc req message to propagate through the system. Since there is
  // no response expected, wait for a minimal amount of time.
  ASSERT_TRUE(timeout_after(ZX_SEC(120), [&]() { return ctx.assoc_received; }));
  ASSERT_TRUE(!ctx.assoc_req.has_value());
  ASSERT_EQ(ctx.assoc_confirmed, false);

  // Send assoc request to device and send the conf
  ctx.ignore_assoc = false;
  ctx.assoc_req = {};
  ctx.assoc_received = false;
  ctx.assoc_confirmed = false;
  mlme_proxy.AssociateReq(wlan_mlme::AssociateRequest{
      .rsne = {},
      .vendor_ies = {},
  });
  ASSERT_TRUE(timeout_after(ZX_SEC(120), [&]() { return ctx.assoc_received; }));
  ASSERT_TRUE(ctx.assoc_req.has_value());
  ASSERT_EQ(ctx.assoc_confirmed, true);

  device.EthUnbind();
}

struct EthernetTestFixture : public ::testing::Test {
  void TestEthernetAgainstRole(wlan_info_mac_role_t role);
  void InitDeviceWithRole(wlan_info_mac_role_t role);
  void SetEthernetOnline(uint32_t expected_status = ETHERNET_STATUS_ONLINE) {
    device_.SetControlledPort(::fuchsia::wlan::mlme::SetControlledPortRequest{
        .state = ::fuchsia::wlan::mlme::ControlledPortState::OPEN});
    ASSERT_EQ(ethernet_status_, expected_status);
  }
  void TearDown() override { device_.EthUnbind(); }

  fake_ddk::Bind fake_ddk_;
  wlanif_impl_protocol_ops_t proto_ops_ = EmptyProtoOps();
  wlanif_impl_protocol_t proto_{.ops = &proto_ops_, .ctx = this};
  // Unsafe cast, however, parent is never used as fake_ddk replaces default device manager.
  wlanif::Device device_{reinterpret_cast<zx_device_t*>(&fake_ddk_), proto_};
  ethernet_ifc_protocol_ops_t eth_ops_{};
  ethernet_ifc_protocol_t eth_proto_ = {.ops = &eth_ops_, .ctx = this};
  wlan_info_mac_role_t role_ = WLAN_INFO_MAC_ROLE_CLIENT;
  uint32_t ethernet_status_{0};

  zx::channel mlme_;
};

#define ETH_DEV(c) static_cast<EthernetTestFixture*>(c)
static zx_status_t hook_start(void* ctx, const wlanif_impl_ifc_protocol_t* ifc,
                              zx_handle_t* out_sme_channel) {
  auto [new_sme, new_mlme] = make_channel();
  ETH_DEV(ctx)->mlme_ = std::move(new_mlme);
  *out_sme_channel = new_sme.release();
  return ZX_OK;
}
static void hook_query(void* ctx, wlanif_query_info_t* info) { info->role = ETH_DEV(ctx)->role_; }
static void hook_eth_status(void* ctx, uint32_t status) { ETH_DEV(ctx)->ethernet_status_ = status; }
#undef ETH_DEV

void EthernetTestFixture::InitDeviceWithRole(wlan_info_mac_role_t role) {
  role_ = role;
  proto_ops_.start = hook_start;
  proto_ops_.query = hook_query;
  eth_proto_.ops->status = hook_eth_status;
  ASSERT_EQ(device_.Bind(), ZX_OK);
}

void EthernetTestFixture::TestEthernetAgainstRole(wlan_info_mac_role_t role) {
  InitDeviceWithRole(role);
  device_.EthStart(&eth_proto_);

  SetEthernetOnline();
  wlanif_deauth_indication_t deauth_ind{.reason_code = WLAN_DEAUTH_REASON_AP_INITIATED};
  device_.DeauthenticateInd(&deauth_ind);
  ASSERT_EQ(ethernet_status_, role_ == WLAN_INFO_MAC_ROLE_CLIENT ? 0u : ETHERNET_STATUS_ONLINE);

  SetEthernetOnline();
  wlanif_deauth_confirm_t deauth_conf{};
  device_.DeauthenticateConf(&deauth_conf);
  ASSERT_EQ(ethernet_status_, role_ == WLAN_INFO_MAC_ROLE_CLIENT ? 0u : ETHERNET_STATUS_ONLINE);

  SetEthernetOnline();
  wlanif_disassoc_indication_t disassoc_ind{.reason_code = WLAN_DEAUTH_REASON_AP_INITIATED};
  device_.DisassociateInd(&disassoc_ind);
  ASSERT_EQ(ethernet_status_, role_ == WLAN_INFO_MAC_ROLE_CLIENT ? 0u : ETHERNET_STATUS_ONLINE);

  SetEthernetOnline();
  wlanif_disassoc_confirm_t disassoc_conf{};
  device_.DisassociateConf(&disassoc_conf);
  ASSERT_EQ(ethernet_status_, role_ == WLAN_INFO_MAC_ROLE_CLIENT ? 0u : ETHERNET_STATUS_ONLINE);
}

TEST_F(EthernetTestFixture, ClientIfaceDisablesEthernetOnDisconnect) {
  TestEthernetAgainstRole(WLAN_INFO_MAC_ROLE_CLIENT);
}

TEST_F(EthernetTestFixture, ApIfaceDoesNotAffectEthernetOnClientDisconnect) {
  TestEthernetAgainstRole(WLAN_INFO_MAC_ROLE_AP);
}

TEST_F(EthernetTestFixture, StartThenSetOnline) {
  InitDeviceWithRole(WLAN_INFO_MAC_ROLE_AP);  // role doesn't matter
  device_.EthStart(&eth_proto_);
  ASSERT_EQ(ethernet_status_, 0u);
  SetEthernetOnline();
  ASSERT_EQ(ethernet_status_, ETHERNET_STATUS_ONLINE);
}

TEST_F(EthernetTestFixture, OnlineThenStart) {
  InitDeviceWithRole(WLAN_INFO_MAC_ROLE_AP);  // role doesn't matter
  SetEthernetOnline(0);
  ASSERT_EQ(ethernet_status_, 0u);
  device_.EthStart(&eth_proto_);
  ASSERT_EQ(ethernet_status_, ETHERNET_STATUS_ONLINE);
}
