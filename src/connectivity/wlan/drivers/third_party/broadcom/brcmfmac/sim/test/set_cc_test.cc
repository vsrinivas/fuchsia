// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

class CountryCodeTest : public SimTest {
 public:
  CountryCodeTest() = default;
  void Init();
  void CreateInterface();
  void DeleteInterface();
  zx_status_t SetCountryCode(const wlanphy_country_t* country);
  void GetCountryCode(brcmf_fil_country_le* ccode);
  uint32_t DeviceCount();

 private:
  // SME callbacks
  static wlanif_impl_ifc_protocol_ops_t sme_ops_;
  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};
  std::unique_ptr<SimInterface> client_ifc_;
};

wlanif_impl_ifc_protocol_ops_t CountryCodeTest::sme_ops_ = {};

void CountryCodeTest::Init() { ASSERT_EQ(SimTest::Init(), ZX_OK); }

void CountryCodeTest::CreateInterface() {
  zx_status_t status;

  status = SimTest::CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT, sme_protocol_, &client_ifc_);
  ASSERT_EQ(status, ZX_OK);
}

void CountryCodeTest::DeleteInterface() {
  uint32_t iface_id;
  zx_status_t status;

  iface_id = client_ifc_->iface_id_;
  status = device_->WlanphyImplDestroyIface(iface_id);
  ASSERT_EQ(status, ZX_OK);
}

uint32_t CountryCodeTest::DeviceCount() { return (dev_mgr_->DevicesCount()); }

zx_status_t CountryCodeTest::SetCountryCode(const wlanphy_country_t* country) {
  return device_->WlanphyImplSetCountry(country);
}

// Note that this function is meant for SIM only. It retrieves the internal
// state of the country code setting by bypassing the interfaces.
void CountryCodeTest::GetCountryCode(brcmf_fil_country_le* ccode) {
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->IovarsGet("country", ccode, sizeof(brcmf_fil_country_le));
}

TEST_F(CountryCodeTest, SetDefault) {
  Init();
  CreateInterface();
  DeleteInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

TEST_F(CountryCodeTest, SetCCode) {
  const wlanphy_country_t valid_country = {{'W', 'W'}};
  const wlanphy_country_t invalid_country = {{'X', 'X'}};
  struct brcmf_fil_country_le country_code;
  zx_status_t status;
  uint8_t code;

  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  status = SetCountryCode(&invalid_country);
  ASSERT_NE(status, ZX_OK);
  status = SetCountryCode(&valid_country);
  ASSERT_EQ(status, ZX_OK);
  GetCountryCode(&country_code);
  code = memcmp(valid_country.alpha2, country_code.ccode, WLANPHY_ALPHA2_LEN);
  ASSERT_EQ(code, 0);
  DeleteInterface();
}

}  // namespace wlan::brcmfmac
