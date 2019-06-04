// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The simulated environment of physical world.
//
// In order to support unit-test of the driver code, we need to mimic the
// real-world. See the below diagram, the 'sim-env' module accepts the requests
// from 'unit test' to create a virtual environment and interacts with
// the 'sim-fw', which is a firmware simulation model.
//
//   +-------------+           +------------+
//   |  unit test  | <-------> |   driver   |
//   +-------------+           +------------+
//        ^     \                    ^
//        |        \                 |
//        |           \              |
//        |              \           |
//        v                 \        v
//   +-------------+           +------------+
//   |   sim-env   | <-------> |   sim-fw   |
//   +-------------+           +------------+
//
// In this framework, 'unit test', 'driver' and 'sim-fw' are device-specific
// implementations. 'sim-env' is commonly used for all drivers. Therefore we can
// leverage the fancy features (e.g. RSSI model) in the sim-env for all drivers.
//

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_ENV_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_ENV_H_

#include <stdint.h>
#include <wlan/protocol/ieee80211.h>
#include <wlan/protocol/info.h>

#include <list>

#include "netinet/if_ether.h"
#include "zircon/types.h"

namespace wlan {
namespace testing {

// To simulate an AP. Only keep minimum information for sim-fw to generate
// response for driver.
//
class SimulatedAp {
 public:
  struct SimulatedSecurity {
    enum ieee80211_cipher_suite cipher_suite;
    size_t key_len;
    uint8_t key[32];
  };

  SimulatedAp(const uint8_t bssid[ETH_ALEN], const uint8_t* ssid,
              const size_t ssid_len, uint8_t chan)
      : chan_(chan) {
    memcpy(bssid_, bssid, sizeof(bssid_));

    ssid_.len = std::min(ssid_len, static_cast<size_t>(WLAN_MAX_SSID_LEN));
    memcpy(ssid_.ssid, ssid, ssid_.len);

    memset(&security_, 0, sizeof(security_));
    security_.cipher_suite = IEEE80211_CIPHER_SUITE_NONE;
  }
  ~SimulatedAp() {}

  // When this is not called, the default is open network.
  void SetSecurity(const struct SimulatedSecurity sec);

 private:
  uint8_t chan_;
  uint8_t bssid_[ETH_ALEN];
  wlan_ssid_t ssid_;
  struct SimulatedSecurity security_;
};

// To simulate the physical environment.
//
class SimulatedEnvironment {
 public:
  SimulatedEnvironment() {}
  ~SimulatedEnvironment() {}

  // Add an AP into the environment. This can be called by unit test and sim-fw.
  void AddAp(SimulatedAp* ap) { aps_.push_back(ap); }

  // getter function for sim-fw to generate the scan response.
  std::list<SimulatedAp*> aps() { return aps_; }

 private:
  std::list<SimulatedAp*> aps_;
};

//
// The driver-specific simulated firmware must inherit this class.
//
class SimulatedFirmware {
 public:
  // |env| is passed into the simulated firmware so that the firmware can
  // interact with the simulated environment.
  SimulatedFirmware(SimulatedEnvironment* env) : env_(env) {}
  ~SimulatedFirmware() {}

 protected:
  SimulatedEnvironment* env_;
};

}  // namespace testing
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_ENV_H_
