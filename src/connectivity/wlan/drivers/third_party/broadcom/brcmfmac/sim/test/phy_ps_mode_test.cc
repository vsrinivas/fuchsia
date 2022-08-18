// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <zircon/errors.h>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
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
  zx_status_t SetPsMode(const fuchsia_wlan_common::wire::PowerSaveType* ps_mode);
  void GetPsModeFromFirmware(uint32_t* ps_mode);
  zx_status_t SetPsModeInFirmware(const wlanphy_ps_mode_t* ps_mode);
  zx_status_t ClearCountryCode();
  uint32_t DeviceCountByProtocolId(uint32_t proto_id);

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

uint32_t PhyPsModeTest::DeviceCountByProtocolId(uint32_t proto_id) {
  return (dev_mgr_->DeviceCountByProtocolId(proto_id));
}

zx_status_t PhyPsModeTest::SetPsMode(const fuchsia_wlan_common::wire::PowerSaveType* ps_mode) {
  fidl::Arena fidl_arena;
  auto builder = fuchsia_wlan_wlanphyimpl::wire::WlanphyImplSetPsModeRequest::Builder(fidl_arena);
  builder.ps_mode(*ps_mode);
  auto result = client_.sync().buffer(test_arena_)->SetPsMode(builder.Build());
  EXPECT_TRUE(result.ok());
  if (result->is_error()) {
    return result->error_value();
  }
  return ZX_OK;
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

zx_status_t PhyPsModeTest::SetPsModeInFirmware(const wlanphy_ps_mode_t* ps_mode) {
  EXPECT_NE(ps_mode, nullptr);
  brcmf_simdev* sim = device_->GetSim();
  return brcmf_set_ps_mode(sim->drvr, ps_mode);
}

TEST_F(PhyPsModeTest, SetPsModeIncorrect) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLANPHY_IMPL), 1u);
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);

  // Get the country code and verify that it is set to WW.
  uint32_t fw_ps_mode;
  GetPsModeFromFirmware(&fw_ps_mode);
  ASSERT_EQ(fw_ps_mode, (uint32_t)PM_OFF);

  // Set PS mode but without passing any PS mode to set and verify
  // that it FAILS
  fidl::Arena fidl_arena;
  auto builder = fuchsia_wlan_wlanphyimpl::wire::WlanphyImplSetPsModeRequest::Builder(fidl_arena);
  auto result = client_.sync().buffer(test_arena_)->SetPsMode(builder.Build());
  EXPECT_TRUE(result.ok());
  zx_status_t status = result->is_error() ? result->error_value() : ZX_OK;

  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);

  DeleteInterface();
}

// Test setting PS Mode to invalid and valid values.
TEST_F(PhyPsModeTest, SetPsMode) {
  const auto valid_ps_mode = fuchsia_wlan_common::wire::PowerSaveType::kPsModeBalanced;
  zx_status_t status;
  uint32_t fw_ps_mode;

  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLANPHY_IMPL), 1u);
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);

  // Get the country code and verify that it is set to WW.
  GetPsModeFromFirmware(&fw_ps_mode);
  ASSERT_EQ(fw_ps_mode, (uint32_t)PM_OFF);

  // Set a valid PS mode and verify it succeeds
  status = SetPsMode(&valid_ps_mode);
  ASSERT_EQ(status, ZX_OK);
  GetPsModeFromFirmware(&fw_ps_mode);
  ASSERT_EQ(fw_ps_mode, (uint32_t)PM_FAST);
  DeleteInterface();
}
// Ensure PS Mode set in FW is either OFF or FAST.
TEST_F(PhyPsModeTest, CheckFWPsMode) {
  auto valid_ps_mode = fuchsia_wlan_common::wire::PowerSaveType::kPsModeBalanced;
  zx_status_t status;
  uint32_t fw_ps_mode;

  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLANPHY_IMPL), 1u);
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);

  // Get the country code and verify that it is set to WW.
  GetPsModeFromFirmware(&fw_ps_mode);
  ASSERT_EQ(fw_ps_mode, (uint32_t)PM_OFF);

  // Set PS mode to PS_MODE_BALANCED
  status = SetPsMode(&valid_ps_mode);
  ASSERT_EQ(status, ZX_OK);
  // Verify that it gets set to FAST in FW
  GetPsModeFromFirmware(&fw_ps_mode);
  ASSERT_EQ(fw_ps_mode, (uint32_t)PM_FAST);

  // Set PS mode to PS_MODE_ULTRA_LOW_POWER
  valid_ps_mode = fuchsia_wlan_common::wire::PowerSaveType::kPsModeUltraLowPower;
  status = SetPsMode(&valid_ps_mode);
  ASSERT_EQ(status, ZX_OK);
  // Verify that it gets set to FAST in FW
  GetPsModeFromFirmware(&fw_ps_mode);
  ASSERT_EQ(fw_ps_mode, (uint32_t)PM_FAST);
  // Set PS mode to PS_MODE_LOW_POWER
  valid_ps_mode = fuchsia_wlan_common::wire::PowerSaveType::kPsModeLowPower;
  status = SetPsMode(&valid_ps_mode);
  ASSERT_EQ(status, ZX_OK);
  // Verify that it gets set to FAST in FW
  GetPsModeFromFirmware(&fw_ps_mode);
  ASSERT_EQ(fw_ps_mode, (uint32_t)PM_FAST);

  // Set PS mode to PS_MODE_PERFORMANCE
  valid_ps_mode = fuchsia_wlan_common::wire::PowerSaveType::kPsModePerformance;
  status = SetPsMode(&valid_ps_mode);
  ASSERT_EQ(status, ZX_OK);
  // Verify that it gets set to OFF in FW
  GetPsModeFromFirmware(&fw_ps_mode);
  ASSERT_EQ(fw_ps_mode, (uint32_t)PM_OFF);
  DeleteInterface();
}

// Test Getting PS Mode
TEST_F(PhyPsModeTest, GetPsMode) {
  Init();
  CreateInterface();

  {
    const auto valid_ps_mode = fuchsia_wlan_common::wire::PowerSaveType::kPsModeBalanced;
    const wlanphy_ps_mode_t valid_ps_mode_banjo = {.ps_mode = POWER_SAVE_TYPE_PS_MODE_BALANCED};
    ASSERT_EQ(ZX_OK, SetPsModeInFirmware(&valid_ps_mode_banjo));
    auto result = client_.sync().buffer(test_arena_)->GetPsMode();
    EXPECT_TRUE(result.ok());
    ASSERT_FALSE(result->is_error());
    EXPECT_EQ(result->value()->ps_mode(), valid_ps_mode);
  }

  // Try again, just in case the first one was a default value.
  {
    const auto valid_ps_mode = fuchsia_wlan_common::wire::PowerSaveType::kPsModePerformance;
    const wlanphy_ps_mode_t valid_ps_mode_banjo = {.ps_mode = POWER_SAVE_TYPE_PS_MODE_PERFORMANCE};
    ASSERT_EQ(ZX_OK, SetPsModeInFirmware(&valid_ps_mode_banjo));
    auto result = client_.sync().buffer(test_arena_)->GetPsMode();
    EXPECT_TRUE(result.ok());
    ASSERT_FALSE(result->is_error());
    EXPECT_EQ(result->value()->ps_mode(), valid_ps_mode);
  }
  DeleteInterface();
}
}  // namespace wlan::brcmfmac
