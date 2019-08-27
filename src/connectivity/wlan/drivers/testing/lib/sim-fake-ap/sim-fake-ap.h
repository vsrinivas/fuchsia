// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_FAKE_AP_SIM_FAKE_AP_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_FAKE_AP_SIM_FAKE_AP_H_

#include <lib/zx/time.h>
#include <stdint.h>

#include <array>
#include <list>

#include <wlan/protocol/ieee80211.h>
#include <wlan/protocol/info.h>

#include "netinet/if_ether.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
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

  // Information received when we get a BEACON notification
  struct BeaconInfo {
    // This indicates which beacon id this event corresponds to. Since we can't disable an event
    // once set, it allows us to ignore an out-of-date beacon that has since been disabled.
    uint64_t beacon_id;
  };

  // Information received when we get a notification
  struct Notification {
    enum NotificationType { BEACON } type;
    union {
      struct BeaconInfo beacon_info;
    };
  };

  FakeAp() = delete;

  explicit FakeAp(Environment* environ) : environment_(environ) {}

  FakeAp(Environment* environ, const common::MacAddr& bssid, const wlan_ssid_t& ssid,
         const wlan_channel_t chan)
      : environment_(environ), chan_(chan), bssid_(bssid), ssid_(ssid) {}

  ~FakeAp() = default;

  void SetChannel(const wlan_channel_t& channel) { chan_ = channel; }
  void SetBssid(const common::MacAddr& bssid) { bssid_ = bssid; }
  void SetSsid(const wlan_ssid_t& ssid) { ssid_ = ssid; }

  // When this is not called, the default is open network.
  void SetSecurity(struct Security sec) { security_ = sec; }

  // Start beaconing. Sends first beacon immediately and schedules beacons to occur every
  // beacon_period until disabled.
  void EnableBeacon(zx::duration beacon_period);

  // Stop beaconing.
  void DisableBeacon();

  // StationIfc operations - these are the functions that allow the simulated AP to be used
  // inside of a sim-env environment.
  void Rx(void* pkt) override {}
  void RxBeacon(const wlan_channel_t& channel, const wlan_ssid_t& ssid,
                const common::MacAddr& bssid) override {}
  void ReceiveNotification(void* payload) override;

 private:
  void ScheduleNextBeacon();

  // Event handlers
  void HandleBeaconNotification(const BeaconInfo& info);

  // The environment in which this fake AP is operating.
  Environment* environment_;

  wlan_channel_t chan_;
  common::MacAddr bssid_;
  wlan_ssid_t ssid_;
  struct Security security_ = {.cipher_suite = IEEE80211_CIPHER_SUITE_NONE};

  // Are we currently emitting beacons?
  bool is_beaconing_ = false;

  zx::duration beacon_interval_;

  // A unique identifier used for each request to beacon. Since we can't (currently) disable an
  // event once it's been set, we need a way to identify the beacons we are currently emitting
  // from any that may have been set previously.
  uint64_t beacon_index_ = 0;
};

}  // namespace wlan::simulation

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_FAKE_AP_SIM_FAKE_AP_H_
