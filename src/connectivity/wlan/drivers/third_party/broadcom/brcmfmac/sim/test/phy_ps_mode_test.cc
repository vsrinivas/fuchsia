// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/wlan/phyinfo/c/banjo.h>
#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <zircon/errors.h>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/defs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

class PhyPsModeTest : public SimTest {
 public:
  PhyPsModeTest() = default;
  void Init();
  void CreateInterface();
  void DeleteInterface();
  zx_status_t SetPsMode(const wlanphy_ps_mode_t* ps_mode);
  zx_status_t GetPsMode(wlanphy_ps_mode_t* ps_mode);
  void GetPsModeFromFirmware(uint32_t* ps_mode);
  zx_status_t ClearCountryCode();
  uint32_t DeviceCount();

 private:
  SimInterface client_ifc_;
};

void PhyPsModeTest::Init() { ASSERT_EQ(SimTest::Init(), ZX_OK); }

void PhyPsModeTest::CreateInterface() {
  zx_status_t status;

  status = StartInterface(WLAN_MAC_ROLE_CLIENT, &client_ifc_);
  ASSERT_EQ(status, ZX_OK);
}

void PhyPsModeTest::DeleteInterface() { EXPECT_EQ(SimTest::DeleteInterface(&client_ifc_), ZX_OK); }

uint32_t PhyPsModeTest::DeviceCount() { return (dev_mgr_->DeviceCount()); }

zx_status_t PhyPsModeTest::SetPsMode(const wlanphy_ps_mode_t* ps_mode) {
  return device_->WlanphyImplSetPsMode(ps_mode);
}

zx_status_t PhyPsModeTest::GetPsMode(wlanphy_ps_mode_t* ps_mode) {
  return device_->WlanphyImplGetPsMode(ps_mode);
}
// Note that this function is meant for SIM only. It retrieves the internal
// state of PS Mode setting by bypassing the interfaces.
void PhyPsModeTest::GetPsModeFromFirmware(uint32_t* ps_mode) {
  EXPECT_NE(ps_mode, nullptr);
  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
  zx_status_t status = brcmf_fil_cmd_int_get(ifp, BRCMF_C_GET_PM, ps_mode, nullptr);
  EXPECT_EQ(status, ZX_OK);
}

// Test setting PS Mode to invalid and valid values.
TEST_F(PhyPsModeTest, SetPsMode) {
  const wlanphy_ps_mode_t valid_ps_mode = {.ps_mode = POWER_SAVE_TYPE_PS_MODE_BALANCED};
  const wlanphy_ps_mode_t invalid_ps_mode = {.ps_mode = 0xFF};
  zx_status_t status;
  uint32_t fw_ps_mode;

  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));

  // Get the country code and verify that it is set to WW.
  GetPsModeFromFirmware(&fw_ps_mode);
  ASSERT_EQ(fw_ps_mode, (uint32_t)PM_OFF);

  // Set an invalid PS mode and verify it fails
  status = SetPsMode(&invalid_ps_mode);
  ASSERT_NE(status, ZX_OK);

  // Verify that it stays with the default
  GetPsModeFromFirmware(&fw_ps_mode);
  ASSERT_EQ(fw_ps_mode, (uint32_t)PM_OFF);
  // Set a valid PS mode and verify it succeeds
  status = SetPsMode(&valid_ps_mode);
  ASSERT_EQ(status, ZX_OK);
  GetPsModeFromFirmware(&fw_ps_mode);
  ASSERT_EQ(fw_ps_mode, (uint32_t)PM_FAST);
  DeleteInterface();
}

// Test Getting PS Mode
TEST_F(PhyPsModeTest, GetPsMode) {
  Init();
  CreateInterface();

  {
    const wlanphy_ps_mode_t valid_ps_mode = {.ps_mode = POWER_SAVE_TYPE_PS_MODE_BALANCED};
    wlanphy_ps_mode_t get_ps_mode;
    ASSERT_EQ(ZX_OK, SetPsMode(&valid_ps_mode));
    ASSERT_EQ(ZX_OK, device_->WlanphyImplGetPsMode(&get_ps_mode));
    EXPECT_EQ(get_ps_mode.ps_mode, valid_ps_mode.ps_mode);
  }

  // Try again, just in case the first one was a default value.
  {
    const wlanphy_ps_mode_t valid_ps_mode = {.ps_mode = POWER_SAVE_TYPE_PS_MODE_PERFORMANCE};
    wlanphy_ps_mode_t get_ps_mode;
    ASSERT_EQ(ZX_OK, SetPsMode(&valid_ps_mode));
    ASSERT_EQ(ZX_OK, device_->WlanphyImplGetPsMode(&get_ps_mode));
    EXPECT_EQ(get_ps_mode.ps_mode, valid_ps_mode.ps_mode);
  }
  DeleteInterface();
}
}  // namespace wlan::brcmfmac
