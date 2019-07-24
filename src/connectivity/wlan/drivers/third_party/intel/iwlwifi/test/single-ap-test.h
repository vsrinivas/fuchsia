// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SINGLE_AP_TEST_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SINGLE_AP_TEST_H_

#include <zircon/assert.h>
#include <zircon/status.h>

#include "gtest/gtest.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/trans-sim.h"

namespace wlan {
namespace testing {

// Helper class for unit test code to inherit in order to create an environment
// with one virtual AP ready to scan/connect.
//
class SingleApTest : public ::testing::Test {
 public:
  SingleApTest() : ap_(kApAddr, kSsid, kSsidLen, kChannel), trans_(&env_) {
    zx_status_t status = trans_.Init();
    ZX_ASSERT_MSG(ZX_OK == status, "Transportation initialization failed: %s",
                  zx_status_get_string(status));
    env_.AddAp(&ap_);
  }
  ~SingleApTest() {}

 protected:
  static constexpr uint8_t kApAddr[ETH_ALEN] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
  static constexpr uint8_t kSsid[] = "MySSID";
  static constexpr size_t kSsidLen = 6;  // The length of 'ssid' above.
  static constexpr uint8_t kChannel = 11;

  SimulatedEnvironment env_;
  SimulatedAp ap_;
  TransportSim trans_;
};

}  // namespace testing
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SINGLE_AP_TEST_H_
