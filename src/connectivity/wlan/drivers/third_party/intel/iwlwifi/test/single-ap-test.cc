// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"

#include <zircon/assert.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/tlv-fw-builder.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/img.h"
}  // extern "C"

namespace wlan::testing {

const common::MacAddr SingleApTest::default_macaddr_(kApAddr);

SingleApTest::SingleApTest()
    : fake_parent_(MockDevice::FakeRootParent()),
      ap_(&env_, default_macaddr_, kSsid, kChannel),
      sim_trans_(&env_, fake_parent_.get()) {
  // Add a default MVM firmware to the fake DDK.
  TlvFwBuilder fw_builder;

  const uint32_t dummy_ucode = 0;
  fw_builder.AddValue(IWL_UCODE_TLV_SEC_INIT, &dummy_ucode, sizeof(dummy_ucode));
  fw_builder.AddValue(IWL_UCODE_TLV_INST, &dummy_ucode, sizeof(dummy_ucode));
  fw_builder.AddValue(IWL_UCODE_TLV_DATA, &dummy_ucode, sizeof(dummy_ucode));
  fw_builder.AddValue(IWL_UCODE_TLV_INIT, &dummy_ucode, sizeof(dummy_ucode));
  fw_builder.AddValue(IWL_UCODE_TLV_INIT_DATA, &dummy_ucode, sizeof(dummy_ucode));

  const uint32_t ucode_phy_sku =
      cpu_to_le32((3 << FW_PHY_CFG_TX_CHAIN_POS) |  // Tx antenna 1 and 0.
                  (6 << FW_PHY_CFG_RX_CHAIN_POS));  // Rx antenna 2 and 1.
  fw_builder.AddValue(IWL_UCODE_TLV_PHY_SKU, &ucode_phy_sku, sizeof(ucode_phy_sku));

  fake_parent_->SetFirmware(fw_builder.GetBinary());

  zx_status_t status = sim_trans_.Init();
  ZX_ASSERT_MSG(ZX_OK == status, "Transportation initialization failed: %s",
                zx_status_get_string(status));
  env_.AddStation(&ap_);
}

SingleApTest::~SingleApTest() = default;

}  // namespace wlan::testing
