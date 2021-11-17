// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_UCODE_CAPA_TEST_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_UCODE_CAPA_TEST_H_

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
class FakeUcodeCapaTest : public ::zxtest::Test {
 public:
  // The constructor takes two parameters, they will be assigned to the two fields struct
  // iwl_ucode_capa, api_index indicates the offset of api_capa in iwl_ucode_capabilities._capa, the
  // value of api_index is usually 0 for 8265. api_capa is the 4-byte flag indicates the supported
  // ucode capabilities of this firmware.
  //
  // For how iwlwifi driver is parsing the ucode capabilities from firmware tlv binary, please refer
  // to iwl_set_ucode_capabilities() in iwlwifi/iwl-drv.c. For the value options of api_capa flags,
  // please refer to enum iwl_ucode_tlv_capa in iwlwifi/fw/file.h.
  FakeUcodeCapaTest(uint32_t api_index, uint32_t api_capa);
  ~FakeUcodeCapaTest() = default;

 protected:
  std::shared_ptr<MockDevice> fake_parent_;
  SimTransport sim_trans_;
};

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_UCODE_CAPA_TEST_H_
