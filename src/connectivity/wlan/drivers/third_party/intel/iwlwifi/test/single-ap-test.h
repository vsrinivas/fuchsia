// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SINGLE_AP_TEST_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SINGLE_AP_TEST_H_

#include <zircon/status.h>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/trans-sim.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace wlan::testing {

// Helper class for unit test code to inherit in order to create an environment
// with one virtual AP ready to scan/connect.
//
// Note that the contructor will call the init function of transportation layer,
// and assert it is successful. The test case doesn't need to init again.
//
class SingleApTest : public ::zxtest::Test {
 public:
  SingleApTest();
  ~SingleApTest() override;
  ;

 protected:
  static constexpr std::array<uint8_t, common::kMacAddrLen> kApAddr = {0x12, 0x34, 0x56,
                                                                       0x78, 0x9a, 0xbc};
  static constexpr cssid_t kSsid = {.len = 6 /* strlen("MySSID") */, .data = "MySSID"};
  static constexpr wlan_channel_t kChannel = {.primary = 11, .cbw = CHANNEL_BANDWIDTH_CBW20};

  static const common::MacAddr default_macaddr_;

  std::shared_ptr<MockDevice> fake_parent_;
  ::wlan::simulation::Environment env_;
  ::wlan::simulation::FakeAp ap_;
  TransportSim sim_trans_;
};

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SINGLE_AP_TEST_H_
