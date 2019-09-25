// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SINGLE_AP_TEST_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SINGLE_AP_TEST_H_

#include <zircon/assert.h>
#include <zircon/status.h>

#include "gtest/gtest.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/trans-sim.h"

namespace wlan::testing {

// Helper class for unit test code to inherit in order to create an environment
// with one virtual AP ready to scan/connect.
//
// Note that the contructor will call the init function of transportation layer,
// and assert it is successful. The test case doesn't need to init again.
//
class SingleApTest : public ::testing::Test {
 public:
  SingleApTest() : ap_(&env_, default_macaddr_, kSsid, kChannel), sim_trans_(&env_) {
    zx_status_t status = sim_trans_.Init();
    ZX_ASSERT_MSG(ZX_OK == status, "Transportation initialization failed: %s",
                  zx_status_get_string(status));
    env_.AddStation(&ap_);
  }
  ~SingleApTest() {}

 protected:
  static constexpr std::array<uint8_t, common::kMacAddrLen> kApAddr = {0x12, 0x34, 0x56,
                                                                       0x78, 0x9a, 0xbc};
  static constexpr wlan_ssid_t kSsid = {.ssid = "MySSID", .len = 6 /* strlen("MySSID") */};
  static constexpr wlan_channel_t kChannel = {.primary = 11, .cbw = WLAN_CHANNEL_BANDWIDTH__20};

  static const common::MacAddr default_macaddr_;

  ::wlan::simulation::Environment env_;
  ::wlan::simulation::FakeAp ap_;
  TransportSim sim_trans_;
};

const common::MacAddr SingleApTest::default_macaddr_(kApAddr);

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SINGLE_AP_TEST_H_
