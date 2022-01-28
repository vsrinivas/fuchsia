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

#include <fuchsia/hardware/wlan/fullmac/c/banjo.h>
#include <fuchsia/hardware/wlan/phyinfo/c/banjo.h>
#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <zircon/errors.h>

#include <algorithm>
#include <memory>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_device.h"

namespace wlan {
namespace brcmfmac {
namespace {

std::pair<zx::channel, zx::channel> make_channel() {
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  return {std::move(local), std::move(remote)};
}

TEST(LifecycleTest, StartStop) {
  auto env = std::make_shared<simulation::Environment>();
  auto dev_mgr = std::make_unique<simulation::FakeDevMgr>();

  SimDevice* device;
  zx_status_t status = SimDevice::Create(dev_mgr->GetRootDevice(), dev_mgr.get(), env, &device);
  ASSERT_EQ(status, ZX_OK);
  status = device->BusInit();
  ASSERT_EQ(status, ZX_OK);
}

TEST(LifecycleTest, StartWithSmeChannel) {
  auto env = std::make_shared<simulation::Environment>();
  auto dev_mgr = std::make_unique<simulation::FakeDevMgr>();

  // Create PHY.
  SimDevice* device;
  zx_status_t status = SimDevice::Create(dev_mgr->GetRootDevice(), dev_mgr.get(), env, &device);
  ASSERT_EQ(status, ZX_OK);
  status = device->BusInit();
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(dev_mgr->DeviceCountByProtocolId(ZX_PROTOCOL_WLANPHY_IMPL), 1u);

  // Create iface.
  auto [local, _remote] = make_channel();
  wlanphy_impl_create_iface_req_t create_iface_req{.role = WLAN_MAC_ROLE_CLIENT,
                                                   .mlme_channel = local.get()};
  uint16_t iface_id;
  status = device->WlanphyImplCreateIface(&create_iface_req, &iface_id);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(dev_mgr->DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);

  // Simulate start call from Fuchsia's generic wlanif-impl driver.
  auto iface = dev_mgr->FindLatestByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL);
  ASSERT_NE(iface, nullptr);
  void* ctx = iface->DevArgs().ctx;
  auto* iface_ops = static_cast<wlan_fullmac_impl_protocol_ops_t*>(iface->DevArgs().proto_ops);
  zx_handle_t mlme_channel = ZX_HANDLE_INVALID;
  wlan_fullmac_impl_ifc_protocol_t ifc_ops{};
  status = iface_ops->start(ctx, &ifc_ops, &mlme_channel);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(mlme_channel, local.get());

  // Verify calling start again will fail with proper error code.
  mlme_channel = ZX_HANDLE_INVALID;
  status = iface_ops->start(ctx, &ifc_ops, &mlme_channel);
  EXPECT_EQ(status, ZX_ERR_ALREADY_BOUND);
  EXPECT_EQ(mlme_channel, ZX_HANDLE_INVALID);
  ASSERT_EQ(device->WlanphyImplDestroyIface(iface_id), ZX_OK);
  EXPECT_EQ(dev_mgr->DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 0u);
}

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan
