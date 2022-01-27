// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-nvm.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-drv.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/fake-ucode-test.h"

namespace wlan::testing {
namespace {

class NvmTest : public FakeUcodeTest {
 public:
  NvmTest() : FakeUcodeTest(0, BIT(IWL_UCODE_TLV_CAPA_LAR_SUPPORT), 0, 0) {}
  ~NvmTest() {}
};

// Please refer the default NVM data in sim-default-nvm.cc. The expected values in this test
// are based on those binary data.
TEST_F(NvmTest, TestParsingDefaultNvm) {
  auto mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans());
  auto data = mvm->nvm_data;
  EXPECT_EQ(data->nvm_version, 0xc16);

  // Compare the parsed MAC address
  uint8_t expected_mac[] = {0xb4, 0xd5, 0xbd, 0x11, 0x22, 0x33};
  EXPECT_BYTES_EQ(expected_mac, data->hw_addr, sizeof(expected_mac));
  EXPECT_EQ(data->n_hw_addrs, 5);

  // Radio config
  EXPECT_EQ(data->radio_cfg_type, NVM_RF_CFG_TYPE_MSK(0x33d0));

  // Features
  EXPECT_EQ(data->sku_cap_band_24ghz_enable, true);
  EXPECT_EQ(data->sku_cap_band_52ghz_enable, true);
  EXPECT_EQ(data->sku_cap_11n_enable, true);
  EXPECT_EQ(data->sku_cap_11ac_enable, true);
  EXPECT_EQ(data->sku_cap_mimo_disabled, false);

  EXPECT_EQ(data->calib_version, 255);

  // Band and channel info
  // - 2G band
  EXPECT_EQ(data->bands[WLAN_INFO_BAND_TWO_GHZ].band, WLAN_INFO_BAND_TWO_GHZ);
  EXPECT_EQ(data->bands[WLAN_INFO_BAND_TWO_GHZ].n_channels, 14);
  EXPECT_EQ(data->bands[WLAN_INFO_BAND_TWO_GHZ].channels[0].ch_num, 1);
  // - 5G band
  EXPECT_EQ(data->bands[WLAN_INFO_BAND_FIVE_GHZ].band, WLAN_INFO_BAND_FIVE_GHZ);
  EXPECT_EQ(data->bands[WLAN_INFO_BAND_FIVE_GHZ].n_channels, 25);
  EXPECT_EQ(data->bands[WLAN_INFO_BAND_FIVE_GHZ].channels[0].ch_num, 36);
}

TEST_F(NvmTest, CfgRatesTo80211) {
  EXPECT_EQ(2, cfg_rates_to_80211(1 * 10));  // 1 Mbps
}

TEST_F(NvmTest, UpdateMcc) {
  auto mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans());
  struct iwl_mcc_update_resp *resp;

  mtx_lock(&mvm->mutex);
  EXPECT_EQ(ZX_OK, iwl_mvm_update_mcc(mvm, "US", MCC_SOURCE_WIFI, &resp));
  mtx_unlock(&mvm->mutex);

  EXPECT_EQ(resp->mcc, 0x5553);  // "US"

  free(resp);
}

TEST_F(NvmTest, InitMcc) {
  auto mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans());

  mtx_lock(&mvm->mutex);
  EXPECT_EQ(ZX_OK, iwl_mvm_init_mcc(mvm));
  mtx_unlock(&mvm->mutex);
  EXPECT_EQ(true, mvm->lar_regdom_set);
}

}  // namespace
}  // namespace wlan::testing
