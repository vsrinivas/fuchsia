// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

class WmmStatusTest : public SimTest {
 public:
  WmmStatusTest() = default;
  void Init();

  SimInterface client_ifc_;
  bool on_wmm_status_resp_called_ = false;

  // SME callbacks
  static wlan_fullmac_impl_ifc_protocol_ops_t sme_ops_;
  wlan_fullmac_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};

  // Event handlers
  void OnWmmStatusResp(zx_status_t status, const wlan_wmm_params_t* resp);
};

// Since we're acting as wlanif, we need handlers for any protocol calls we may receive
wlan_fullmac_impl_ifc_protocol_ops_t WmmStatusTest::sme_ops_ = {
    .on_wmm_status_resp =
        [](void* ctx, zx_status_t status, const wlan_wmm_params_t* resp) {
          static_cast<WmmStatusTest*>(ctx)->OnWmmStatusResp(status, resp);
        },
};

void WmmStatusTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &client_ifc_, &sme_protocol_), ZX_OK);
  on_wmm_status_resp_called_ = false;
}

void WmmStatusTest::OnWmmStatusResp(zx_status_t status, const wlan_wmm_params_t* resp) {
  ASSERT_EQ(status, ZX_OK);

  EXPECT_TRUE(resp->apsd);

  EXPECT_EQ(resp->ac_be_params.aifsn, 4);
  EXPECT_EQ(resp->ac_be_params.ecw_min, 5);
  EXPECT_EQ(resp->ac_be_params.ecw_max, 10);
  EXPECT_EQ(resp->ac_be_params.txop_limit, 0);
  EXPECT_FALSE(resp->ac_be_params.acm);

  EXPECT_EQ(resp->ac_bk_params.aifsn, 7);
  EXPECT_EQ(resp->ac_bk_params.ecw_min, 6);
  EXPECT_EQ(resp->ac_bk_params.ecw_max, 11);
  EXPECT_EQ(resp->ac_bk_params.txop_limit, 0);
  EXPECT_FALSE(resp->ac_bk_params.acm);

  EXPECT_EQ(resp->ac_vi_params.aifsn, 3);
  EXPECT_EQ(resp->ac_vi_params.ecw_min, 4);
  EXPECT_EQ(resp->ac_vi_params.ecw_max, 5);
  EXPECT_EQ(resp->ac_vi_params.txop_limit, 94);
  EXPECT_FALSE(resp->ac_vi_params.acm);

  EXPECT_EQ(resp->ac_vo_params.aifsn, 2);
  EXPECT_EQ(resp->ac_vo_params.ecw_min, 2);
  EXPECT_EQ(resp->ac_vo_params.ecw_max, 4);
  EXPECT_EQ(resp->ac_vo_params.txop_limit, 47);
  EXPECT_TRUE(resp->ac_vo_params.acm);

  on_wmm_status_resp_called_ = true;
}

TEST_F(WmmStatusTest, WmmStatus) {
  Init();

  client_ifc_.if_impl_ops_->wmm_status_req(client_ifc_.if_impl_ctx_);
  EXPECT_TRUE(on_wmm_status_resp_called_);
}

}  // namespace wlan::brcmfmac
