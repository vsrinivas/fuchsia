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

#include <fuchsia/hardware/wlan/info/c/banjo.h>
#include <fuchsia/hardware/wlanif/c/banjo.h>
#include <lib/ddk/device.h>
#include <zircon/errors.h>

#include <algorithm>
#include <memory>

#include <ddk/hw/wlan/wlaninfo/c/banjo.h>
#include <gtest/gtest.h>

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

namespace wlan {
namespace brcmfmac {
namespace {

class FirmwareConfigTest : public SimTest {
 public:
  static constexpr bool kArpNdOffloadEnabled = true;
  static constexpr bool kArpNdOffloadDisabled = false;
  void ArpNdOffloadConfigValidate(wlan_info_mac_role_t role, bool expect_enabled);
};

void FirmwareConfigTest::ArpNdOffloadConfigValidate(wlan_info_mac_role_t role, bool expected) {
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

std::pair<zx::channel, zx::channel> make_channel() {
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  return {std::move(local), std::move(remote)};
}

TEST_F(FirmwareConfigTest, StartWithSmeChannel) {
  auto env = std::make_shared<simulation::Environment>();
  auto dev_mgr = std::make_unique<simulation::FakeDevMgr>();

  // Create PHY.
  SimDevice* device;
  zx_status_t status = SimDevice::Create(dev_mgr->GetRootDevice(), dev_mgr.get(), env, &device);
  ASSERT_EQ(status, ZX_OK);
  status = device->Init();
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(dev_mgr->DeviceCount(), static_cast<size_t>(1));

  // Create iface.
  auto [local, _remote] = make_channel();
  wlanphy_impl_create_iface_req_t create_iface_req{.role = WLAN_INFO_MAC_ROLE_CLIENT,
                                                   .sme_channel = local.get()};
  uint16_t iface_id;
  status = device->WlanphyImplCreateIface(&create_iface_req, &iface_id);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(dev_mgr->DeviceCount(), static_cast<size_t>(2));

  // Simulate start call from Fuchsia's generic wlanif-impl driver.
  auto iface = dev_mgr->FindFirstByProtocolId(ZX_PROTOCOL_WLANIF_IMPL);
  ASSERT_TRUE(iface.has_value());
  void* ctx = iface->dev_args.ctx;
  auto* iface_ops = static_cast<wlanif_impl_protocol_ops_t*>(iface->dev_args.proto_ops);
  zx_handle_t sme_channel = ZX_HANDLE_INVALID;
  wlanif_impl_ifc_protocol_t ifc_ops{};
  status = iface_ops->start(ctx, &ifc_ops, &sme_channel);
  EXPECT_EQ(status, ZX_OK);

  brcmf_simdev* sim = device->GetSim();
  EXPECT_EQ(sim->sim_fw->GetPM(), PM_OFF);
  ASSERT_EQ(device->WlanphyImplDestroyIface(iface_id), ZX_OK);
}

TEST_F(FirmwareConfigTest, ArpNdOffloadClientConfigTestWithoutSoftApFeat) {
  Init();

  // When SoftAP feature is unavailable, we expect driver to enable arp/nd offload.
  device_->GetSim()->drvr->feat_flags &= (!BIT(BRCMF_FEAT_AP));
  ArpNdOffloadConfigValidate(WLAN_INFO_MAC_ROLE_CLIENT, kArpNdOffloadEnabled);
}

TEST_F(FirmwareConfigTest, ArpNdOffloadClientConfigTestWithSoftApFeat) {
  Init();

  // When SoftAP feature is available, we expect driver to not enable arp/nd offload.
  device_->GetSim()->drvr->feat_flags |= BIT(BRCMF_FEAT_AP);
  ArpNdOffloadConfigValidate(WLAN_INFO_MAC_ROLE_CLIENT, kArpNdOffloadDisabled);
}

TEST_F(FirmwareConfigTest, ArpNdOffloadApConfigTestWithSoftApFeat) {
  Init();
  device_->GetSim()->drvr->feat_flags |= BIT(BRCMF_FEAT_AP);
  ArpNdOffloadConfigValidate(WLAN_INFO_MAC_ROLE_AP, kArpNdOffloadDisabled);
}

TEST_F(FirmwareConfigTest, MchanDisabledClient) {
  Init();
  SimInterface ifc;
  uint32_t iovar;

  EXPECT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &ifc), ZX_OK);
  struct brcmf_if* ifp = brcmf_get_ifp(device_->GetSim()->drvr, ifc.iface_id_);
  brcmf_fil_iovar_int_get(ifp, "mchan", &iovar, nullptr);
  EXPECT_EQ(kMchanState, iovar);
}

TEST_F(FirmwareConfigTest, MchanDisabledSoftAp) {
  Init();
  SimInterface ifc;
  uint32_t iovar;

  EXPECT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_AP, &ifc), ZX_OK);
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

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan
