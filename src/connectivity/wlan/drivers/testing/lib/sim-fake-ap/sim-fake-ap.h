// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_FAKE_AP_SIM_FAKE_AP_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_FAKE_AP_SIM_FAKE_AP_H_

#include <stdint.h>

#include <array>
#include <list>

#include <wlan/protocol/ieee80211.h>
#include <wlan/protocol/info.h>

#include "netinet/if_ether.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"
#include "zircon/types.h"

namespace wlan::simulation {

// To simulate an AP. Only keep minimum information for sim-fw to generate
// response for driver.
//
class FakeAp : public StationIfc {
 public:
  struct Security {
    enum ieee80211_cipher_suite cipher_suite;

    static constexpr size_t kMaxKeyLen = 32;
    size_t key_len;
    std::array<uint8_t, kMaxKeyLen> key;
  };

  typedef std::array<uint8_t, ETH_ALEN> Bssid;

  FakeAp(const Bssid bssid, const wlan_ssid_t ssid, const wlan_channel_t chan)
      : chan_(chan),
        bssid_(bssid),
        ssid_(ssid),
        security_{.cipher_suite = IEEE80211_CIPHER_SUITE_NONE} {}
  ~FakeAp() = default;

  // When this is not called, the default is open network.
  void SetSecurity(struct Security sec);

  // StationIfc operations - these are the functions that allow the simulated AP to be used
  // inside of a sim-env environment.
  void Rx(void* pkt) override {}
  void RxBeacon(wlan_channel_t* channel, wlan_ssid_t* ssid) override {}
  void ReceiveNotification(enum EnvironmentEventType notification_type, void* payload) override {}

 private:
  wlan_channel_t chan_;
  Bssid bssid_;
  wlan_ssid_t ssid_;
  struct Security security_;
};

}  // namespace wlan::simulation

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_FAKE_AP_SIM_FAKE_AP_H_
