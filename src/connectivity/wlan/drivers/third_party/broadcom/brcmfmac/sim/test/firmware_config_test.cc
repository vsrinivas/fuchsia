/*
 * Copyright (c) 2019 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <fuchsia/hardware/wlan/associnfo/c/banjo.h>
#include <fuchsia/hardware/wlan/fullmac/c/banjo.h>
#include <lib/ddk/device.h>
#include <zircon/errors.h>

#include <algorithm>
#include <memory>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/defs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/feature.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "zircon/system/ulib/sync/include/lib/sync/cpp/completion.h"

namespace wlan::brcmfmac {
namespace {

class FirmwareConfigTest : public SimTest {
 public:
  static constexpr bool kArpNdOffloadEnabled = true;
  static constexpr bool kArpNdOffloadDisabled = false;
  void ArpNdOffloadConfigValidate(wlan_mac_role_t role, bool expect_enabled);
};

void FirmwareConfigTest::ArpNdOffloadConfigValidate(wlan_mac_role_t role, bool expected) {
  uint32_t mode = 0;
  bool arpoe = false;
  bool ndoe = false;
  SimInterface ifc;
  uint32_t iovar;

  if (expected == kArpNdOffloadEnabled) {
    mode = BRCMF_ARP_OL_AGENT | BRCMF_ARP_OL_PEER_AUTO_REPLY;
    arpoe = true;
    ndoe = true;
  }

  EXPECT_EQ(StartInterface(role, &ifc), ZX_OK);

  brcmf_pub* drvr = device_->GetSim()->drvr;
  struct brcmf_if* ifp = brcmf_get_ifp(drvr, ifc.iface_id_);
  brcmf_fil_iovar_int_get(ifp, "arp_ol", &iovar, nullptr);
  EXPECT_EQ(mode, iovar);

  brcmf_fil_iovar_int_get(ifp, "arpoe", &iovar, nullptr);
  EXPECT_EQ(arpoe, iovar);

  brcmf_fil_iovar_int_get(ifp, "ndoe", &iovar, nullptr);
  EXPECT_EQ(ndoe, iovar);
}

TEST_F(FirmwareConfigTest, ArpNdOffloadClientConfigTestWithoutSoftApFeat) {
  Init();

  // When SoftAP feature is unavailable, we expect driver to enable arp/nd offload.
  device_->GetSim()->drvr->feat_flags &= (!BIT(BRCMF_FEAT_AP));
  ArpNdOffloadConfigValidate(WLAN_MAC_ROLE_CLIENT, kArpNdOffloadEnabled);
}

TEST_F(FirmwareConfigTest, ArpNdOffloadClientConfigTestWithSoftApFeat) {
  Init();

  // When SoftAP feature is available, we expect driver to not enable arp/nd offload.
  device_->GetSim()->drvr->feat_flags |= BIT(BRCMF_FEAT_AP);
  ArpNdOffloadConfigValidate(WLAN_MAC_ROLE_CLIENT, kArpNdOffloadDisabled);
}

TEST_F(FirmwareConfigTest, ArpNdOffloadApConfigTestWithSoftApFeat) {
  Init();
  device_->GetSim()->drvr->feat_flags |= BIT(BRCMF_FEAT_AP);
  ArpNdOffloadConfigValidate(WLAN_MAC_ROLE_AP, kArpNdOffloadDisabled);
}

TEST_F(FirmwareConfigTest, WnmDisabledClient) {
  Init();
  SimInterface ifc;
  uint32_t iovar;

  EXPECT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &ifc), ZX_OK);
  struct brcmf_if* ifp = brcmf_get_ifp(device_->GetSim()->drvr, ifc.iface_id_);
  const auto get_status = brcmf_fil_iovar_int_get(ifp, "wnm", &iovar, nullptr);
  ASSERT_EQ(get_status, ZX_OK);
  EXPECT_EQ(0U, iovar);
}

TEST_F(FirmwareConfigTest, MchanDisabledClient) {
  Init();
  SimInterface ifc;
  uint32_t iovar;

  EXPECT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &ifc), ZX_OK);
  struct brcmf_if* ifp = brcmf_get_ifp(device_->GetSim()->drvr, ifc.iface_id_);
  brcmf_fil_iovar_int_get(ifp, "mchan", &iovar, nullptr);
  EXPECT_EQ(kMchanState, iovar);
}

TEST_F(FirmwareConfigTest, MchanDisabledSoftAp) {
  Init();
  SimInterface ifc;
  uint32_t iovar;

  EXPECT_EQ(StartInterface(WLAN_MAC_ROLE_AP, &ifc), ZX_OK);
  struct brcmf_if* ifp = brcmf_get_ifp(device_->GetSim()->drvr, ifc.iface_id_);
  brcmf_fil_iovar_int_get(ifp, "mchan", &iovar, nullptr);
  EXPECT_EQ(kMchanState, iovar);
}

TEST_F(FirmwareConfigTest, StbcTxAndTxstreams) {
  Init();

  struct brcmf_if* ifp = brcmf_get_ifp(device_->GetSim()->drvr, 0);
  int32_t stbc_tx = 1;
  // Since default txstreams is 1, setting stbc_tx to 1 should fail
  zx_status_t status = brcmf_fil_iovar_int_set(ifp, "stbc_tx", stbc_tx, nullptr);
  EXPECT_NE(status, ZX_OK);
  // Since default txstreams is 1, setting stbc_tx to 0 should succeed
  stbc_tx = 0;
  status = brcmf_fil_iovar_int_set(ifp, "stbc_tx", stbc_tx, nullptr);
  EXPECT_EQ(status, ZX_OK);
  // Now set txstreams to 2
  uint32_t txstreams = 2;
  status = brcmf_fil_iovar_int_set(ifp, "txstreams", txstreams, nullptr);
  EXPECT_EQ(status, ZX_OK);
  // Since txstreams is now 2, setting stbc_tx to 1 should succeed
  stbc_tx = 1;
  status = brcmf_fil_iovar_int_set(ifp, "stbc_tx", stbc_tx, nullptr);
  EXPECT_EQ(status, ZX_OK);
}

TEST_F(FirmwareConfigTest, RoamEngineDisabledByDefault) {
  Init();
  SimInterface ifc;
  uint32_t iovar;

  EXPECT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &ifc), ZX_OK);
  struct brcmf_if* ifp = brcmf_get_ifp(device_->GetSim()->drvr, ifc.iface_id_);
  const auto get_status = brcmf_fil_iovar_int_get(ifp, "roam_off", &iovar, nullptr);

  ASSERT_EQ(get_status, ZX_OK);
  EXPECT_EQ(1U, iovar);
}

TEST_F(FirmwareConfigTest, RoamEngineEnabledViaFeatureFlag) {
  Init();
  SimInterface ifc;
  uint32_t iovar;

  device_->GetSim()->drvr->feat_flags |= BIT(BRCMF_FEAT_ROAM_ENGINE);
  EXPECT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &ifc), ZX_OK);
  struct brcmf_if* ifp = brcmf_get_ifp(device_->GetSim()->drvr, ifc.iface_id_);
  const auto get_status = brcmf_fil_iovar_int_get(ifp, "roam_off", &iovar, nullptr);

  ASSERT_EQ(get_status, ZX_OK);
  EXPECT_EQ(0U, iovar);
}

TEST_F(FirmwareConfigTest, BeaconTimeoutDefault) {
  Init();
  SimInterface ifc;
  uint32_t iovar;

  EXPECT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &ifc), ZX_OK);
  struct brcmf_if* ifp = brcmf_get_ifp(device_->GetSim()->drvr, ifc.iface_id_);
  const auto get_status = brcmf_fil_iovar_int_get(ifp, "bcn_timeout", &iovar, nullptr);

  ASSERT_EQ(get_status, ZX_OK);
  EXPECT_EQ(8U, iovar);
}

TEST_F(FirmwareConfigTest, BeaconTimeoutWhenRoamEngineIsSetup) {
  Init();
  SimInterface ifc;
  uint32_t iovar;

  device_->GetSim()->drvr->feat_flags |= BIT(BRCMF_FEAT_ROAM_ENGINE);
  EXPECT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &ifc), ZX_OK);
  struct brcmf_if* ifp = brcmf_get_ifp(device_->GetSim()->drvr, ifc.iface_id_);
  const auto get_status = brcmf_fil_iovar_int_get(ifp, "bcn_timeout", &iovar, nullptr);

  ASSERT_EQ(get_status, ZX_OK);
  // Driver uses different value for beacon timeout if roam engine is enabled.
  EXPECT_EQ(2U, iovar);
}

}  // namespace
}  // namespace wlan::brcmfmac
