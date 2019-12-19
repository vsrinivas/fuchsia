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
  // How to handle an association request
  enum AssocHandling { ASSOC_ALLOWED, ASSOC_IGNORED, ASSOC_REJECTED };

  struct Security {
    enum ieee80211_cipher_suite cipher_suite;

    static constexpr size_t kMaxKeyLen = 32;
    size_t key_len;
    std::array<uint8_t, kMaxKeyLen> key;
  };

  FakeAp() = delete;

  explicit FakeAp(Environment* environ) : environment_(environ) { environ->AddStation(this); }

  FakeAp(Environment* environ, const common::MacAddr& bssid, const wlan_ssid_t& ssid,
         const wlan_channel_t chan)
      : environment_(environ), chan_(chan), bssid_(bssid), ssid_(ssid) {
    environ->AddStation(this);
  }

  ~FakeAp() = default;

  void SetChannel(const wlan_channel_t& channel) { chan_ = channel; }
  void SetBssid(const common::MacAddr& bssid) { bssid_ = bssid; }
  void SetSsid(const wlan_ssid_t& ssid) { ssid_ = ssid; }

  wlan_channel_t GetChannel() { return chan_; }
  common::MacAddr GetBssid() { return bssid_; }
  wlan_ssid_t GetSsid() { return ssid_; }
  uint32_t GetNumClients() { return clients_.size(); }

  // Will we receive a message sent on the specified channel?
  bool CanReceiveChannel(const wlan_channel_t& channel);

  // When this is not called, the default is open network.
  void SetSecurity(struct Security sec) { security_ = sec; }

  // Start beaconing. Sends first beacon immediately and schedules beacons to occur every
  // beacon_period until disabled.
  void EnableBeacon(zx::duration beacon_period);

  // Stop beaconing.
  void DisableBeacon();

  // Specify how to handle association requests
  void SetAssocHandling(enum AssocHandling mode);

  // Disassociate a Station
  zx_status_t DisassocSta(const common::MacAddr& sta_mac, uint16_t reason);

  // StationIfc operations - these are the functions that allow the simulated AP to be used
  // inside of a sim-env environment.
  void Rx(void* pkt) override {}
  void RxBeacon(const wlan_channel_t& channel, const wlan_ssid_t& ssid,
                const common::MacAddr& bssid) override {}
  void RxAssocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                  const common::MacAddr& bssid) override;
  void RxAssocResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, uint16_t status) override {}
  void RxDisassocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, uint16_t reason) override;
  void RxProbeReq(const wlan_channel_t& channel, const common::MacAddr& src) override;
  void RxProbeResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, const wlan_ssid_t& ssid) override {}
  void ReceiveNotification(void* payload) override;

 private:
  void ScheduleNextBeacon();
  void ScheduleAssocResp(uint16_t status, const common::MacAddr& dst);
  void ScheduleProbeResp(const common::MacAddr& dst);

  // Event handlers
  void HandleBeaconNotification();
  void HandleAssocRespNotification(uint16_t status, common::MacAddr dst);
  void HandleProbeRespNotification(common::MacAddr dst);

  // The environment in which this fake AP is operating.
  Environment* environment_;

  wlan_channel_t chan_;
  common::MacAddr bssid_;
  wlan_ssid_t ssid_;
  struct Security security_ = {.cipher_suite = IEEE80211_CIPHER_SUITE_NONE};

  struct BeaconState {
    // Are we currently emitting beacons?
    bool is_beaconing = false;

    zx::duration beacon_interval;

    // Unique value that is associated with the next beacon event
    uint64_t beacon_notification_id;
  } beacon_state_;

  enum AssocHandling assoc_handling_mode_ = ASSOC_ALLOWED;

  // Delay between an association request and an association response
  zx::duration assoc_resp_interval_ = zx::msec(1);
  // Delay between an Disassociation request and an Disassociation response
  zx::duration disassoc_resp_interval_ = zx::msec(1);
  // Delay between an probe request and an probe response
  zx::duration probe_resp_interval_ = zx::msec(1);

  std::list<common::MacAddr> clients_;
};

}  // namespace wlan::simulation

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_FAKE_AP_SIM_FAKE_AP_H_
