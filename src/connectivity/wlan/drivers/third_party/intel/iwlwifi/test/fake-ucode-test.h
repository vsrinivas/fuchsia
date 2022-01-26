// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_UCODE_TEST_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_UCODE_TEST_H_

#include <memory>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-trans.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace wlan::testing {

// Helper class for unit test code to inherit in order to create an fake firmware with
// customized ucode capability.
//
// Note that the contructor will call the init function of transportation layer,
// and assert it is successful. The test case doesn't need to init again.
//
class FakeUcodeTest : public ::zxtest::Test {
 public:
  // The constructor takes four parameters. They will be assigned to the API flags and capabilities.
  // The *_index are the array index in iwl_ucode_capabilities._api[] and ._capa[]. The *_value are
  // limited from 0 to 31. These 2 fields are used to present all values of IWL_UCODE_TLV_API_* and
  // IWL_UCODE_TLV_CAPA_*.
  //
  // TODO(fxbug.dev/92106): support multiple bits.
  //
  // For how iwlwifi driver is parsing the ucode capabilities from firmware tlv binary, please refer
  // to iwl_set_ucode_capabilities() in iwlwifi/iwl-drv.c. For the value options, please refer to
  // enum iwl_ucode_tlv_capa and enum iwl_ucode_tlv_api in iwlwifi/fw/file.h.
  FakeUcodeTest(uint32_t capa_index, uint32_t capa_val, uint32_t api_index, uint32_t api_val);
  ~FakeUcodeTest() = default;

 protected:
  std::shared_ptr<MockDevice> fake_parent_;
  SimTransport sim_trans_;
};

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_UCODE_TEST_H_
