// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/wlanif/device.h"

#include <fuchsia/wlan/internal/c/banjo.h>
#include <fuchsia/wlan/internal/cpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/decoder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <zircon/errors.h>

#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <tuple>
#include <vector>

#include <ddk/hw/wlan/ieee80211/c/banjo.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fuchsia/hardware/wlan/fullmac/c/banjo.h"
#include "fuchsia/wlan/common/c/banjo.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "test_bss.h"

namespace wlan_internal = ::fuchsia::wlan::internal;
namespace wlan_mlme = ::fuchsia::wlan::mlme;

using ::testing::_;
using ::testing::ElementsAre;

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
// wlan_fullmac_impl->set_muilticast_promisc.
TEST(MulticastPromiscMode, OnOff) {
  zx_status_t status;

  wlan_fullmac_impl_protocol_ops_t proto_ops = {.set_multicast_promisc =
                                                    hook_set_multicast_promisc};
  wlan_fullmac_impl_protocol_t proto = {.ops = &proto_ops};
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

  wlan_fullmac_impl_protocol_ops_t proto_ops = {};
  wlan_fullmac_impl_protocol_t proto = {.ops = &proto_ops};
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

  ~SmeChannelTestContext() {
    auto scan_req = this->scan_req;
    if (scan_req.has_value()) {
      if (scan_req->channels_list != nullptr) {
        delete[] const_cast<uint8_t*>(scan_req->channels_list);
      }
      if (scan_req->ssids_list != nullptr) {
        delete[] const_cast<cssid_t*>(scan_req->ssids_list);
      }
    }
  }

  void CaptureIncomingScanRequest(const wlan_fullmac_scan_req_t* req) {
    std::unique_ptr<uint8_t[]> channels_list_begin;
    std::unique_ptr<cssid_t[]> cssids_list_begin;

    this->scan_req = *req;

    // Copy the dynamically allocated contents of wlan_fullmac_scan_req_t.
    if (req->channels_count > 0) {
      channels_list_begin = std::make_unique<uint8_t[]>(req->channels_count);
      if (channels_list_begin == nullptr) {
        FAIL();
      }
      memcpy(channels_list_begin.get(), req->channels_list, req->channels_count * sizeof(uint8_t));
    }
    this->scan_req->channels_list = channels_list_begin.release();  // deleted in destructor

    if (req->ssids_count > 0) {
      cssids_list_begin = std::make_unique<cssid_t[]>(req->ssids_count);
      if (cssids_list_begin == nullptr) {
        FAIL();
      }
      memcpy(cssids_list_begin.get(), req->ssids_list, req->ssids_count * sizeof(cssid_t));
    }
    this->scan_req->ssids_list = cssids_list_begin.release();  // deleted in destructor
  }

  zx::channel mlme = {};
  zx::channel sme = {};
  std::optional<wlan_fullmac_scan_req_t> scan_req = {};
};

wlan_fullmac_impl_protocol_ops_t EmptyProtoOps() {
  return wlan_fullmac_impl_protocol_ops_t{
      // Each instance is required to provide its own .start() method to store the MLME channels.
      // SME Channel will be provided to wlanif-impl-driver when it calls back into its parent.
      .query = [](void* ctx, wlan_fullmac_query_info_t* info) { memset(info, 0, sizeof(*info)); },
      .query_mac_sublayer_support =
          [](void* ctx, mac_sublayer_support_t* resp) { memset(resp, 0, sizeof(*resp)); },
      .query_security_support = [](void* ctx,
                                   security_support_t* resp) { memset(resp, 0, sizeof(*resp)); },
      .query_spectrum_management_support =
          [](void* ctx, spectrum_management_support_t* resp) { memset(resp, 0, sizeof(*resp)); },
      .start_scan = [](void* ctx, const wlan_fullmac_scan_req_t* req) {},
      .join_req = [](void* ctx, const wlan_fullmac_join_req_t* req) {},
      .auth_req = [](void* ctx, const wlan_fullmac_auth_req_t* req) {},
      .auth_resp = [](void* ctx, const wlan_fullmac_auth_resp_t* req) {},
      .deauth_req = [](void* ctx, const wlan_fullmac_deauth_req_t* req) {},
      .assoc_req = [](void* ctx, const wlan_fullmac_assoc_req_t* req) {},
      .assoc_resp = [](void* ctx, const wlan_fullmac_assoc_resp_t* req) {},
      .disassoc_req = [](void* ctx, const wlan_fullmac_disassoc_req_t* req) {},
      .reset_req = [](void* ctx, const wlan_fullmac_reset_req_t* req) {},
      .start_req = [](void* ctx, const wlan_fullmac_start_req_t* req) {},
      .stop_req = [](void* ctx, const wlan_fullmac_stop_req_t* req) {},
      .set_keys_req = [](void* ctx, const wlan_fullmac_set_keys_req_t* req,
                         wlan_fullmac_set_keys_resp_t* resp) {},
      .del_keys_req = [](void* ctx, const wlan_fullmac_del_keys_req_t* req) {},
      .eapol_req = [](void* ctx, const wlan_fullmac_eapol_req_t* req) {},
  };
}

#define SME_DEV(c) static_cast<SmeChannelTestContext*>(c)

wlan_mlme::ScanRequest fake_mlme_scan_request(std::vector<uint8_t>&& channel_list,
                                              std::vector<std::vector<uint8_t>>&& ssid_list) {
  return {
      .txn_id = 754,
      .scan_type = wlan_mlme::ScanTypes::PASSIVE,
      .channel_list = std::move(channel_list),
      .ssid_list = std::move(ssid_list),
      .probe_delay = 0,
      .min_channel_time = 0,
      .max_channel_time = 100,
  };
}

TEST(SmeChannel, ScanRequest) {
  wlan_fullmac_impl_protocol_ops_t proto_ops = EmptyProtoOps();
  proto_ops.start = [](void* ctx, const wlan_fullmac_impl_ifc_protocol_t* ifc,
                       zx_handle_t* out_mlme_channel) -> zx_status_t {
    *out_mlme_channel = SME_DEV(ctx)->sme.release();
    return ZX_OK;
  };

  // Capture incoming scan request.
  proto_ops.start_scan = [](void* ctx, const wlan_fullmac_scan_req_t* req) {
    SME_DEV(ctx)->CaptureIncomingScanRequest(req);
  };

  SmeChannelTestContext ctx;
  wlan_fullmac_impl_protocol_t proto = {
      .ops = &proto_ops,
      .ctx = &ctx,
  };

  auto parent = MockDevice::FakeRootParent();
  // The parent calls release on this pointer which will delete it so don't delete it or manage it.
  auto device = new wlanif::Device{parent.get(), proto};
  auto status = device->Bind();
  ASSERT_EQ(status, ZX_OK);

  // Send scan request to device.
  auto mlme_proxy = wlan_mlme::MLME_SyncProxy(std::move(ctx.mlme));
  wlan_mlme::ScanRequest mlme_scan_request =
      fake_mlme_scan_request({1, 36}, {{1, 2, 3}, {4, 5, 6, 7}});
  mlme_proxy.StartScan(mlme_scan_request);

  // Wait for scan message to propagate through the system.
  ASSERT_TRUE(timeout_after(ZX_SEC(120), [&]() { return ctx.scan_req.has_value(); }));

  // Verify scan request.
  ASSERT_TRUE(ctx.scan_req.has_value());
  ASSERT_EQ(ctx.scan_req->txn_id, 754u);
  ASSERT_EQ(ctx.scan_req->scan_type, WLAN_SCAN_TYPE_PASSIVE);

  ASSERT_EQ(ctx.scan_req->channels_count, 2u);
  uint8_t expected_channels_list[] = {1, 36};
  ASSERT_EQ(0,
            std::memcmp(ctx.scan_req->channels_list, expected_channels_list, 2 * sizeof(uint8_t)));
  ASSERT_EQ(ctx.scan_req->ssids_count, 2u);
  ASSERT_EQ(ctx.scan_req->ssids_list[0].len, 3);
  ASSERT_THAT(ctx.scan_req->ssids_list[0].data,
              ElementsAre(1, 2, 3, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _,
                          _, _, _, _, _, _, _));
  ASSERT_EQ(ctx.scan_req->ssids_list[1].len, 4);
  ASSERT_THAT(ctx.scan_req->ssids_list[1].data,
              ElementsAre(4, 5, 6, 7, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _,
                          _, _, _, _, _, _, _));
  ASSERT_EQ(ctx.scan_req->min_channel_time, 0u);
  ASSERT_EQ(ctx.scan_req->max_channel_time, 100u);

  device->Unbind();
}

TEST(SmeChannel, ScanRequestEmptyChannelListFails) {
  wlan_fullmac_impl_protocol_ops_t proto_ops = EmptyProtoOps();
  proto_ops.start = [](void* ctx, const wlan_fullmac_impl_ifc_protocol_t* ifc,
                       zx_handle_t* out_mlme_channel) -> zx_status_t {
    *out_mlme_channel = SME_DEV(ctx)->sme.release();
    return ZX_OK;
  };

  // Capture incoming scan request.
  proto_ops.start_scan = [](void* ctx, const wlan_fullmac_scan_req_t* req) {
    SME_DEV(ctx)->CaptureIncomingScanRequest(req);
  };

  SmeChannelTestContext ctx;
  wlan_fullmac_impl_protocol_t proto = {
      .ops = &proto_ops,
      .ctx = &ctx,
  };

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  auto mlme_ptr = wlan_mlme::MLMEPtr();
  std::optional<wlan_mlme::ScanEnd> scan_end = {};
  mlme_ptr.Bind(std::move(ctx.mlme), dispatcher);
  mlme_ptr.events().OnScanEnd = [&scan_end, &loop](wlan_mlme::ScanEnd returned_scan_end) {
    scan_end.emplace(std::move(returned_scan_end));
    loop.Quit();
  };

  auto parent = MockDevice::FakeRootParent();
  // The parent calls release on this pointer which will delete it so don't delete it or manage it.
  auto device = new wlanif::Device{parent.get(), proto};
  auto status = device->Bind();
  ASSERT_EQ(status, ZX_OK);

  // Send scan request to device.
  wlan_mlme::ScanRequest mlme_scan_request = fake_mlme_scan_request({}, {{1, 2, 3}, {4, 5, 6, 7}});
  mlme_ptr->StartScan(mlme_scan_request);
  loop.Run();

  // Verify no scan request sent and ScanEnd value.
  ASSERT_FALSE(ctx.scan_req.has_value());
  ASSERT_TRUE(scan_end.has_value());
  ASSERT_EQ(scan_end.value().txn_id, 754u);
  ASSERT_EQ(scan_end.value().code, wlan_mlme::ScanResultCode::INVALID_ARGS);

  device->Unbind();
}

TEST(SmeChannel, ScanRequestEmptySsidList) {
  wlan_fullmac_impl_protocol_ops_t proto_ops = EmptyProtoOps();
  proto_ops.start = [](void* ctx, const wlan_fullmac_impl_ifc_protocol_t* ifc,
                       zx_handle_t* out_mlme_channel) -> zx_status_t {
    *out_mlme_channel = SME_DEV(ctx)->sme.release();
    return ZX_OK;
  };

  //  Capture incoming scan request.
  proto_ops.start_scan = [](void* ctx, const wlan_fullmac_scan_req_t* req) {
    SME_DEV(ctx)->CaptureIncomingScanRequest(req);
  };

  SmeChannelTestContext ctx;
  wlan_fullmac_impl_protocol_t proto = {
      .ops = &proto_ops,
      .ctx = &ctx,
  };

  auto parent = MockDevice::FakeRootParent();
  // The parent calls release on this pointer which will delete it so don't delete it or manage it.
  auto device = new wlanif::Device{parent.get(), proto};
  auto status = device->Bind();
  ASSERT_EQ(status, ZX_OK);

  // Send scan request to device.
  auto mlme_proxy = wlan_mlme::MLME_SyncProxy(std::move(ctx.mlme));
  wlan_mlme::ScanRequest mlme_scan_request = fake_mlme_scan_request({1, 2, 3, 4, 5}, {});
  mlme_proxy.StartScan(mlme_scan_request);

  // Wait for scan message to propagate through the system.
  ASSERT_TRUE(timeout_after(ZX_SEC(120), [&]() { return ctx.scan_req.has_value(); }));

  // Verify scan request.
  ASSERT_TRUE(ctx.scan_req.has_value());
  ASSERT_EQ(ctx.scan_req->txn_id, 754u);
  ASSERT_EQ(ctx.scan_req->scan_type, WLAN_SCAN_TYPE_PASSIVE);
  ASSERT_EQ(ctx.scan_req->channels_count, 5u);
  uint8_t expected_channels_list[] = {1, 2, 3, 4, 5};
  ASSERT_EQ(0,
            std::memcmp(ctx.scan_req->channels_list, expected_channels_list, 5 * sizeof(uint8_t)));
  ASSERT_EQ(ctx.scan_req->ssids_count, 0u);
  ASSERT_EQ(ctx.scan_req->min_channel_time, 0u);
  ASSERT_EQ(ctx.scan_req->max_channel_time, 100u);

  device->Unbind();
}

#undef SME_DEV

// Tests that the device will be unbound following a failed device bind.
TEST(SmeChannel, FailedBind) {
  wlan_fullmac_impl_protocol_ops_t proto_ops = EmptyProtoOps();
  proto_ops.start = [](void* ctx, const wlan_fullmac_impl_ifc_protocol_t* ifc,
                       zx_handle_t* out_mlme_channel) -> zx_status_t {
    *out_mlme_channel = static_cast<SmeChannelTestContext*>(ctx)->sme.release();
    return ZX_OK;
  };

  SmeChannelTestContext ctx;
  wlan_fullmac_impl_protocol_t proto = {
      .ops = &proto_ops,
      .ctx = &ctx,
  };

  auto parent = MockDevice::FakeRootParent();
  // The parent calls release on this pointer which will delete it so don't delete it or manage it.
  auto device = new wlanif::Device{parent.get(), proto};

  // Connect a mock channel so that the next device bind will fail.
  zx::channel local, remote;
  ASSERT_EQ(zx::channel::create(0, &local, &remote), ZX_OK);
  ASSERT_EQ(device->Connect(std::move(remote)), ZX_OK);

  // This should fail and request the device be unbound.
  auto status = device->Bind();
  ASSERT_NE(status, ZX_OK);
  mock_ddk::ReleaseFlaggedDevices(parent.get());
  ASSERT_EQ(0u, parent->descendant_count());
}

struct AssocReqTestContext {
  AssocReqTestContext() {
    auto [new_sme, new_mlme] = make_channel();
    mlme = std::move(new_mlme);
    sme = std::move(new_sme);
  }

  zx::channel mlme = {};
  zx::channel sme = {};
  std::optional<wlan_fullmac_assoc_req_t> assoc_req = {};
  wlan_fullmac_impl_ifc_protocol_t ifc = {};
  volatile std::atomic<bool> assoc_received = false;
  volatile std::atomic<bool> assoc_confirmed = false;
  volatile std::atomic<bool> ignore_assoc = false;
};

TEST(AssocReqHandling, MultipleAssocReq) {
#define ASSOC_DEV(c) static_cast<AssocReqTestContext*>(c)
  static wlan_fullmac_impl_ifc_protocol_ops_t wlan_fullmac_impl_ifc_ops = {
      // MLME operations
      .assoc_conf =
          [](void* cookie, const wlan_fullmac_assoc_confirm_t* resp) {
            ASSOC_DEV(cookie)->assoc_confirmed = true;
          },
  };

  wlan_fullmac_impl_protocol_ops_t proto_ops = EmptyProtoOps();
  proto_ops.start = [](void* ctx, const wlan_fullmac_impl_ifc_protocol_t* ifc,
                       zx_handle_t* out_mlme_channel) -> zx_status_t {
    *out_mlme_channel = ASSOC_DEV(ctx)->sme.release();
    // Substitute with our own ops to capture assoc conf
    ASSOC_DEV(ctx)->ifc.ops = &wlan_fullmac_impl_ifc_ops;
    ASSOC_DEV(ctx)->ifc.ctx = ctx;
    return ZX_OK;
  };

  proto_ops.assoc_req = [](void* ctx, const wlan_fullmac_assoc_req_t* req) {
    if (ASSOC_DEV(ctx)->ignore_assoc == false) {
      ASSOC_DEV(ctx)->assoc_req = {{
          .rsne_len = req->rsne_len,
          .vendor_ie_len = req->vendor_ie_len,
      }};
      wlan_fullmac_assoc_confirm_t conf;

      conf.result_code = 0;
      conf.association_id = 1;
      wlan_fullmac_impl_ifc_assoc_conf(&ASSOC_DEV(ctx)->ifc, &conf);
    }
    ASSOC_DEV(ctx)->assoc_received = true;
  };
  AssocReqTestContext ctx;
  wlan_fullmac_impl_protocol_t proto = {
      .ops = &proto_ops,
      .ctx = &ctx,
  };

  auto parent = MockDevice::FakeRootParent();
  // The parent calls release on this pointer which will delete it so don't delete it or manage it.
  auto device = new wlanif::Device{parent.get(), proto};
  auto status = device->Bind();
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

  device->Unbind();
}

struct DeviceTestFixture : public ::gtest::TestLoopFixture {
  void InitDevice();
  void TearDown() override {
    device_->Unbind();
    TestLoopFixture::TearDown();
  }
  zx_status_t HookStart(const wlan_fullmac_impl_ifc_protocol_t* ifc,
                        zx_handle_t* out_mlme_channel) {
    auto [new_sme, new_mlme] = make_channel();

    zx_status_t status = mlme_.Bind(std::move(new_sme), dispatcher());
    if (status != ZX_OK) {
      return status;
    }

    wlan_fullmac_impl_ifc_ = *ifc;
    *out_mlme_channel = new_mlme.release();
    return ZX_OK;
  }
  std::shared_ptr<MockDevice> parent_ = MockDevice::FakeRootParent();
  wlan_fullmac_impl_protocol_ops_t proto_ops_ = EmptyProtoOps();
  wlan_fullmac_impl_protocol_t proto_{.ops = &proto_ops_, .ctx = this};
  // The parent calls release on this pointer which will delete it so don't delete it or manage it.
  wlanif::Device* device_{new wlanif::Device(parent_.get(), proto_)};
  wlan_fullmac_impl_ifc_protocol_t wlan_fullmac_impl_ifc_{};

  fuchsia::wlan::mlme::MLMEPtr mlme_;
};

#define DEV(c) static_cast<DeviceTestFixture*>(c)
static zx_status_t hook_start(void* ctx, const wlan_fullmac_impl_ifc_protocol_t* ifc,
                              zx_handle_t* out_mlme_channel) {
  return DEV(ctx)->HookStart(ifc, out_mlme_channel);
}
#undef DEV

void DeviceTestFixture::InitDevice() {
  proto_ops_.start = hook_start;
  ASSERT_EQ(device_->Bind(), ZX_OK);
}

TEST_F(DeviceTestFixture, SetKeysSuccess) {
  InitDevice();
  proto_ops_.set_keys_req = [](void* ctx, const wlan_fullmac_set_keys_req_t* req,
                               wlan_fullmac_set_keys_resp_t* out_resp) {
    for (size_t key_num = 0; key_num < req->num_keys; key_num++) {
      out_resp->statuslist[key_num] = ZX_OK;
    }
    out_resp->num_keys = req->num_keys;
  };
  fuchsia::wlan::mlme::SetKeysConfirm conf;
  mlme_.events().SetKeysConf = [&](fuchsia::wlan::mlme::SetKeysConfirm set_keys_conf) {
    conf = std::move(set_keys_conf);
  };
  fuchsia::wlan::mlme::SetKeysRequest req{
      .keylist = {fuchsia::wlan::mlme::SetKeyDescriptor{
                      .key_id = 1,
                      .key_type = fuchsia::wlan::mlme::KeyType::GROUP,
                  },
                  fuchsia::wlan::mlme::SetKeyDescriptor{
                      .key_id = 2,
                      .key_type = fuchsia::wlan::mlme::KeyType::GROUP,
                  }},
  };
  device_->SetKeysReq(req);
  RunLoopUntilIdle();
  ASSERT_EQ(conf.results.size(), 2ul);
  ASSERT_EQ(conf.results[0].key_id, 1);
  ASSERT_EQ(conf.results[0].status, ZX_OK);
  ASSERT_EQ(conf.results[1].key_id, 2);
  ASSERT_EQ(conf.results[1].status, ZX_OK);
}

TEST_F(DeviceTestFixture, SetKeysPartialFailure) {
  InitDevice();
  proto_ops_.set_keys_req = [](void* ctx, const wlan_fullmac_set_keys_req_t* req,
                               wlan_fullmac_set_keys_resp_t* out_resp) {
    for (size_t key_num = 0; key_num < req->num_keys; key_num++) {
      if (req->keylist[key_num].key_id == 1) {
        out_resp->statuslist[key_num] = ZX_ERR_INTERNAL;
      } else {
        out_resp->statuslist[key_num] = ZX_OK;
      }
    }
    out_resp->num_keys = req->num_keys;
  };
  fuchsia::wlan::mlme::SetKeysConfirm conf;
  mlme_.events().SetKeysConf = [&](fuchsia::wlan::mlme::SetKeysConfirm set_keys_conf) {
    conf = std::move(set_keys_conf);
  };
  fuchsia::wlan::mlme::SetKeysRequest req{
      .keylist = {fuchsia::wlan::mlme::SetKeyDescriptor{
                      .key_id = 0,
                      .key_type = fuchsia::wlan::mlme::KeyType::PEER_KEY,
                  },
                  fuchsia::wlan::mlme::SetKeyDescriptor{
                      .key_id = 1,
                      .key_type = fuchsia::wlan::mlme::KeyType::GROUP,
                  },
                  fuchsia::wlan::mlme::SetKeyDescriptor{
                      .key_id = 2,
                      .key_type = fuchsia::wlan::mlme::KeyType::GROUP,
                  }},
  };
  device_->SetKeysReq(req);
  RunLoopUntilIdle();
  ASSERT_EQ(conf.results.size(), 3ul);
  ASSERT_EQ(conf.results[0].key_id, 0);
  ASSERT_EQ(conf.results[0].status, ZX_OK);
  ASSERT_EQ(conf.results[1].key_id, 1);
  ASSERT_EQ(conf.results[1].status, ZX_ERR_INTERNAL);
  ASSERT_EQ(conf.results[2].key_id, 2);
  ASSERT_EQ(conf.results[2].status, ZX_OK);
}

TEST_F(DeviceTestFixture, SetKeysTooLarge) {
  InitDevice();
  fuchsia::wlan::mlme::SetKeysConfirm conf;
  mlme_.events().SetKeysConf = [&](fuchsia::wlan::mlme::SetKeysConfirm set_keys_conf) {
    conf = std::move(set_keys_conf);
  };
  fuchsia::wlan::mlme::SetKeysRequest req;
  for (uint16_t i = 0; i < static_cast<uint16_t>(WLAN_MAX_KEYLIST_SIZE) + 1; i++) {
    req.keylist.push_back(fuchsia::wlan::mlme::SetKeyDescriptor{
        .key_id = i,
        .key_type = fuchsia::wlan::mlme::KeyType::GROUP,
    });
  }
  device_->SetKeysReq(req);
  RunLoopUntilIdle();
  ASSERT_EQ(conf.results.size(), WLAN_MAX_KEYLIST_SIZE + 1);
  for (size_t i = 0; i < WLAN_MAX_KEYLIST_SIZE + 1; i++) {
    EXPECT_EQ(conf.results[i].status, ZX_ERR_INVALID_ARGS);
  }
}

struct EthernetTestFixture : public DeviceTestFixture {
  void TestEthernetAgainstRole(wlan_mac_role_t role);
  void InitDeviceWithRole(wlan_mac_role_t role);
  void SetEthernetOnline(uint32_t expected_status = ETHERNET_STATUS_ONLINE) {
    device_->SetControlledPort(::fuchsia::wlan::mlme::SetControlledPortRequest{
        .state = ::fuchsia::wlan::mlme::ControlledPortState::OPEN});
    ASSERT_EQ(ethernet_status_, expected_status);
  }
  void SetEthernetOffline(uint32_t expected_status = 0) {
    device_->SetControlledPort(::fuchsia::wlan::mlme::SetControlledPortRequest{
        .state = ::fuchsia::wlan::mlme::ControlledPortState::CLOSED});
    ASSERT_EQ(ethernet_status_, expected_status);
  }
  void CallDataRecv() {
    // Doesn't matter what we put in as argument here (except for device_).
    // The main thing we want to do is make this call `Device::EthRecv` so we can test this
    // doesn't deadlock.
    wlan_fullmac_impl_ifc_.ops->data_recv(device_, nullptr, 0, 0);
  }

  ethernet_ifc_protocol_ops_t eth_ops_{};
  ethernet_ifc_protocol_t eth_proto_ = {.ops = &eth_ops_, .ctx = this};
  wlan_mac_role_t role_ = WLAN_MAC_ROLE_CLIENT;
  uint32_t ethernet_status_{0};
  data_plane_type_t data_plane_type_ = DATA_PLANE_TYPE_ETHERNET_DEVICE;
  std::optional<bool> link_state_;
  std::function<void(const wlan_fullmac_start_req_t*)> start_req_cb_;

  volatile std::atomic<bool> eth_recv_called_ = false;
};

#define ETH_DEV(c) static_cast<EthernetTestFixture*>(c)
static void hook_query(void* ctx, wlan_fullmac_query_info_t* info) {
  info->role = ETH_DEV(ctx)->role_;
}

static void hook_query_mac_sublayer_support(void* ctx, mac_sublayer_support_t* out_resp) {
  out_resp->data_plane.data_plane_type = ETH_DEV(ctx)->data_plane_type_;
}

static void hook_eth_status(void* ctx, uint32_t status) { ETH_DEV(ctx)->ethernet_status_ = status; }
static void hook_eth_recv(void* ctx, const uint8_t* buffer, size_t data_size, uint32_t flags) {
  ETH_DEV(ctx)->eth_recv_called_ = true;
}

static void hook_start_req(void* ctx, const wlan_fullmac_start_req_t* req) {
  if (ETH_DEV(ctx)->start_req_cb_) {
    ETH_DEV(ctx)->start_req_cb_(req);
  }
}

void EthernetTestFixture::InitDeviceWithRole(wlan_mac_role_t role) {
  role_ = role;
  proto_ops_.start = hook_start;
  proto_ops_.query = hook_query;
  proto_ops_.query_mac_sublayer_support = hook_query_mac_sublayer_support;
  proto_ops_.start_req = hook_start_req;
  eth_proto_.ops->status = hook_eth_status;
  eth_proto_.ops->recv = hook_eth_recv;
  ASSERT_EQ(device_->Bind(), ZX_OK);
}

void EthernetTestFixture::TestEthernetAgainstRole(wlan_mac_role_t role) {
  InitDeviceWithRole(role);
  device_->EthStart(&eth_proto_);

  SetEthernetOnline();
  wlan_fullmac_deauth_indication_t deauth_ind{.reason_code = REASON_CODE_AP_INITIATED};
  device_->DeauthenticateInd(&deauth_ind);
  ASSERT_EQ(ethernet_status_, role_ == WLAN_MAC_ROLE_CLIENT ? 0u : ETHERNET_STATUS_ONLINE);

  SetEthernetOnline();
  wlan_fullmac_deauth_confirm_t deauth_conf{};
  device_->DeauthenticateConf(&deauth_conf);
  ASSERT_EQ(ethernet_status_, role_ == WLAN_MAC_ROLE_CLIENT ? 0u : ETHERNET_STATUS_ONLINE);

  SetEthernetOnline();
  wlan_fullmac_disassoc_indication_t disassoc_ind{.reason_code = REASON_CODE_AP_INITIATED};
  device_->DisassociateInd(&disassoc_ind);
  ASSERT_EQ(ethernet_status_, role_ == WLAN_MAC_ROLE_CLIENT ? 0u : ETHERNET_STATUS_ONLINE);

  SetEthernetOnline();
  wlan_fullmac_disassoc_confirm_t disassoc_conf{};
  device_->DisassociateConf(&disassoc_conf);
  ASSERT_EQ(ethernet_status_, role_ == WLAN_MAC_ROLE_CLIENT ? 0u : ETHERNET_STATUS_ONLINE);
}

TEST_F(EthernetTestFixture, ClientIfaceDisablesEthernetOnDisconnect) {
  TestEthernetAgainstRole(WLAN_MAC_ROLE_CLIENT);
}

TEST_F(EthernetTestFixture, ApIfaceDoesNotAffectEthernetOnClientDisconnect) {
  TestEthernetAgainstRole(WLAN_MAC_ROLE_AP);
}

TEST_F(EthernetTestFixture, StartThenSetOnline) {
  InitDeviceWithRole(WLAN_MAC_ROLE_AP);  // role doesn't matter
  device_->EthStart(&eth_proto_);
  ASSERT_EQ(ethernet_status_, 0u);
  SetEthernetOnline();
  ASSERT_EQ(ethernet_status_, ETHERNET_STATUS_ONLINE);
}

TEST_F(EthernetTestFixture, OnlineThenStart) {
  InitDeviceWithRole(WLAN_MAC_ROLE_AP);  // role doesn't matter
  SetEthernetOnline(0);
  ASSERT_EQ(ethernet_status_, 0u);
  device_->EthStart(&eth_proto_);
  ASSERT_EQ(ethernet_status_, ETHERNET_STATUS_ONLINE);
}

TEST_F(EthernetTestFixture, EthernetDataPlane) {
  InitDeviceWithRole(WLAN_MAC_ROLE_CLIENT);

  // The device added should support the ethernet impl protocol
  auto children = parent_->children();
  ASSERT_EQ(children.size(), 1u);
  ethernet_impl_protocol_t eth_impl_proto;
  EXPECT_EQ(device_get_protocol(children.front().get(), ZX_PROTOCOL_ETHERNET_IMPL, &eth_impl_proto),
            ZX_OK);
}

TEST_F(EthernetTestFixture, GndDataPlane) {
  data_plane_type_ = DATA_PLANE_TYPE_GENERIC_NETWORK_DEVICE;
  InitDeviceWithRole(WLAN_MAC_ROLE_CLIENT);

  // The device added should NOT support the ethernet impl protocol
  auto children = parent_->children();
  ASSERT_EQ(children.size(), 1u);
  ethernet_impl_protocol_t eth_impl_proto;
  EXPECT_NE(device_get_protocol(children.front().get(), ZX_PROTOCOL_ETHERNET_IMPL, &eth_impl_proto),
            ZX_OK);
}

TEST_F(EthernetTestFixture, ApOfflineUntilStartConf) {
  start_req_cb_ = [this](const wlan_fullmac_start_req_t* req) {
    // Interface should not be online until start has been confirmed.
    ASSERT_EQ(ethernet_status_, 0u);
    wlan_fullmac_start_confirm_t response{.result_code = WLAN_START_RESULT_SUCCESS};
    wlan_fullmac_impl_ifc_start_conf(&wlan_fullmac_impl_ifc_, &response);
  };
  InitDeviceWithRole(WLAN_MAC_ROLE_AP);
  device_->EthStart(&eth_proto_);

  // Provide our own callback for StartConf to verify the result.
  std::optional<wlan_mlme::StartResultCode> start_result;
  mlme_.events().StartConf = [&](::fuchsia::wlan::mlme::StartConfirm start_conf) {
    start_result = start_conf.result_code;
  };

  ::fuchsia::wlan::mlme::StartRequest req;
  device_->StartReq(req);
  // Now that the StartConf is received the interface should be online.
  ASSERT_EQ(ethernet_status_, ETHERNET_STATUS_ONLINE);

  RunLoopUntilIdle();

  ASSERT_TRUE(start_result.has_value());
  ASSERT_EQ(start_result.value(), wlan_mlme::StartResultCode::SUCCESS);
}

TEST_F(EthernetTestFixture, ApOfflineOnFailedStartConf) {
  start_req_cb_ = [this](const wlan_fullmac_start_req_t* req) {
    // Send a failed start confirm.
    wlan_fullmac_start_confirm_t response{.result_code = WLAN_START_RESULT_NOT_SUPPORTED};
    wlan_fullmac_impl_ifc_start_conf(&wlan_fullmac_impl_ifc_, &response);
  };
  InitDeviceWithRole(WLAN_MAC_ROLE_AP);
  device_->EthStart(&eth_proto_);

  // Provide our own callback for StartConf to verify  theresult.
  std::optional<wlan_mlme::StartResultCode> start_result;
  mlme_.events().StartConf = [&](::fuchsia::wlan::mlme::StartConfirm start_conf) {
    start_result = start_conf.result_code;
  };

  ::fuchsia::wlan::mlme::StartRequest req;
  device_->StartReq(req);
  ASSERT_EQ(ethernet_status_, 0u);

  RunLoopUntilIdle();

  ASSERT_TRUE(start_result.has_value());
  ASSERT_EQ(start_result.value(), wlan_mlme::StartResultCode::NOT_SUPPORTED);
}

TEST_F(EthernetTestFixture, ApSecondStartDoesNotCallImpl) {
  int ap_start_reqs = 0;
  // Verify that if a request is made to start an AP while an AP is already running then the
  // wlanif driver will not forward that request to the wlan_fullmac_impl.
  start_req_cb_ = [&](const wlan_fullmac_start_req_t* req) {
    ++ap_start_reqs;
    wlan_fullmac_start_confirm_t response{.result_code = WLAN_START_RESULT_SUCCESS};
    wlan_fullmac_impl_ifc_start_conf(&wlan_fullmac_impl_ifc_, &response);
  };
  InitDeviceWithRole(WLAN_MAC_ROLE_AP);
  device_->EthStart(&eth_proto_);

  // Provide our own callback for StartConf to verify results.
  std::vector<wlan_mlme::StartResultCode> start_results;
  mlme_.events().StartConf = [&](::fuchsia::wlan::mlme::StartConfirm start_conf) {
    start_results.push_back(start_conf.result_code);
  };

  ::fuchsia::wlan::mlme::StartRequest req;
  device_->StartReq(req);
  ASSERT_EQ(ap_start_reqs, 1);
  ASSERT_EQ(ethernet_status_, ETHERNET_STATUS_ONLINE);

  // Make a second request, the start request should not propagate to our protocol implementation.
  device_->StartReq(req);
  // The number of requests should stay at one and the interface should remain online.
  ASSERT_EQ(ap_start_reqs, 1);
  ASSERT_EQ(ethernet_status_, ETHERNET_STATUS_ONLINE);

  RunLoopUntilIdle();

  // Verify that StartConf was called twice and that the first time succeeded and the second time
  // indicated that the AP was already started.
  ASSERT_EQ(start_results.size(), 2u);
  ASSERT_EQ(start_results[0], wlan_mlme::StartResultCode::SUCCESS);
  ASSERT_EQ(start_results[1], wlan_mlme::StartResultCode::BSS_ALREADY_STARTED_OR_JOINED);
}

TEST_F(EthernetTestFixture, NotifyOnline) {
  proto_ops_.on_link_state_changed = [](void* ctx, bool online) {
    reinterpret_cast<EthernetTestFixture*>(ctx)->link_state_ = online;
  };

  InitDeviceWithRole(WLAN_MAC_ROLE_CLIENT);
  device_->EthStart(&eth_proto_);

  // Setting the device to online should result in a link state change.
  SetEthernetOnline();
  ASSERT_TRUE(link_state_.has_value());
  EXPECT_TRUE(link_state_.value());

  // Clear the optional and then set the status to online again, another link state event should NOT
  // be sent.
  link_state_.reset();
  SetEthernetOnline();
  EXPECT_FALSE(link_state_.has_value());

  // Now set it to offline and verify we get a link state change.
  link_state_.reset();
  SetEthernetOffline();
  ASSERT_TRUE(link_state_.has_value());
  EXPECT_FALSE(link_state_.value());

  // And similarly setting it to offline when it's already offline should NOT send a link state
  // event.
  link_state_.reset();
  SetEthernetOffline();
  EXPECT_FALSE(link_state_.has_value());
}

TEST_F(EthernetTestFixture, GetIfaceCounterStatsReqDoesNotDeadlockWithEthRecv) {
  proto_ops_.get_iface_counter_stats =
      [](void* ctx, wlan_fullmac_iface_counter_stats_t* out_stats) -> int32_t {
    ETH_DEV(ctx)->CallDataRecv();
    return ZX_OK;
  };
  InitDeviceWithRole(WLAN_MAC_ROLE_CLIENT);
  device_->EthStart(&eth_proto_);

  device_->GetIfaceCounterStats([](auto resp) {});
  ASSERT_TRUE(eth_recv_called_);
}

TEST_F(EthernetTestFixture, GetIfaceHistogramStatsReqDoesNotDeadlockWithEthRecv) {
  proto_ops_.get_iface_histogram_stats =
      [](void* ctx, wlan_fullmac_iface_histogram_stats_t* out_stats) -> int32_t {
    ETH_DEV(ctx)->CallDataRecv();
    return ZX_OK;
  };
  InitDeviceWithRole(WLAN_MAC_ROLE_CLIENT);
  device_->EthStart(&eth_proto_);

  device_->GetIfaceHistogramStats([](auto resp) {});
  ASSERT_TRUE(eth_recv_called_);
}

TEST_F(EthernetTestFixture, JoinReqDoesNotDeadlockWithEthRecv) {
  proto_ops_.join_req = [](void* ctx, const wlan_fullmac_join_req_t* req) {
    ETH_DEV(ctx)->CallDataRecv();
  };
  InitDeviceWithRole(WLAN_MAC_ROLE_CLIENT);
  device_->EthStart(&eth_proto_);

  device_->JoinReq(wlan_fullmac_test::CreateJoinReq());
  ASSERT_TRUE(eth_recv_called_);
}

TEST_F(EthernetTestFixture, AuthReqDoesNotDeadlockWithEthRecv) {
  proto_ops_.auth_req = [](void* ctx, const wlan_fullmac_auth_req_t* req) {
    ETH_DEV(ctx)->CallDataRecv();
  };
  InitDeviceWithRole(WLAN_MAC_ROLE_CLIENT);
  device_->EthStart(&eth_proto_);

  device_->AuthenticateReq(wlan_fullmac_test::CreateAuthenticateReq());
  ASSERT_TRUE(eth_recv_called_);
}

TEST_F(EthernetTestFixture, DeauthReqDoesNotDeadlockWithEthRecv) {
  proto_ops_.deauth_req = [](void* ctx, const wlan_fullmac_deauth_req_t* req) {
    ETH_DEV(ctx)->CallDataRecv();
  };
  InitDeviceWithRole(WLAN_MAC_ROLE_CLIENT);
  device_->EthStart(&eth_proto_);

  device_->DeauthenticateReq(wlan_fullmac_test::CreateDeauthenticateReq());
  ASSERT_TRUE(eth_recv_called_);
}

TEST_F(EthernetTestFixture, DisassociateReqDoesNotDeadlockWithEthRecv) {
  proto_ops_.disassoc_req = [](void* ctx, const wlan_fullmac_disassoc_req_t* req) {
    ETH_DEV(ctx)->CallDataRecv();
  };
  InitDeviceWithRole(WLAN_MAC_ROLE_CLIENT);
  device_->EthStart(&eth_proto_);

  wlan_mlme::DisassociateRequest disassoc_req;
  device_->DisassociateReq(disassoc_req);
  ASSERT_TRUE(eth_recv_called_);
}

TEST_F(EthernetTestFixture, StartReqDoesNotDeadlockWithEthRecv) {
  start_req_cb_ = [this](const wlan_fullmac_start_req_t* req) { this->CallDataRecv(); };
  InitDeviceWithRole(WLAN_MAC_ROLE_CLIENT);
  device_->EthStart(&eth_proto_);

  device_->StartReq(wlan_fullmac_test::CreateStartReq());
  ASSERT_TRUE(eth_recv_called_);
}

TEST_F(EthernetTestFixture, StopReqDoesNotDeadlockWithEthRecv) {
  proto_ops_.stop_req = [](void* ctx, const wlan_fullmac_stop_req_t* req) {
    ETH_DEV(ctx)->CallDataRecv();
  };
  InitDeviceWithRole(WLAN_MAC_ROLE_CLIENT);
  device_->EthStart(&eth_proto_);

  device_->StopReq(wlan_fullmac_test::CreateStopReq());
  ASSERT_TRUE(eth_recv_called_);
}

#undef ETH_DEV
