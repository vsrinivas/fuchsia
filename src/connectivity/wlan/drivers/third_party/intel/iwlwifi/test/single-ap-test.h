// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SINGLE_AP_TEST_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SINGLE_AP_TEST_H_

#include <zircon/status.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-trans.h"
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

 protected:
  static constexpr cssid_t kSsid = {.len = 6 /* strlen("MySSID") */, .data = "MySSID"};
  static constexpr wlan_channel_t kChannel = {.primary = 11, .cbw = CHANNEL_BANDWIDTH_CBW20};

  std::shared_ptr<MockDevice> fake_parent_;
  SimTransport sim_trans_;
};

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SINGLE_AP_TEST_H_
