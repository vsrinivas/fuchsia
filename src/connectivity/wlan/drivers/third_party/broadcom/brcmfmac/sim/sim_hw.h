/*
 * Copyright (c) 2019 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_HW_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_HW_H_

#include <net/ethernet.h>
#include <zircon/status.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil_types.h"

namespace wlan::brcmfmac {

using RxBeaconHandler = std::function<void(const wlan_channel_t& channel, const wlan_ssid_t& ssid,
                                           const common::MacAddr& bssid)>;

class SimHardware : simulation::StationIfc {
 public:
  struct EventHandlers {
    RxBeaconHandler rx_beacon_handler;
  };

  explicit SimHardware(simulation::Environment* env);

  // Tells us how to call the SimFirmware instance
  void SetCallbacks(const EventHandlers& handlers);

  void EnableRx() { rx_enabled_ = true; }
  void DisableRx() { rx_enabled_ = false; }

  void SetChannel(uint8_t channel) { channel_ = channel; }

  void GetRevInfo(brcmf_rev_info_le* rev_info);

  void RequestCallback(std::function<void()>* callback, zx::duration delay);

  // StationIfc methods
  void Rx(void* pkt) override {}  // no-op
  void RxBeacon(const wlan_channel_t& channel, const wlan_ssid_t& ssid,
                const common::MacAddr& bssid) override;
  void RxAssocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                  const common::MacAddr& bssid) override {}  // no-op
  void RxAssocResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, uint16_t status) override {}               // no-op
  void RxProbeReq(const wlan_channel_t& channel, const common::MacAddr& src) override {}  // no-op
  void RxProbeResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, const wlan_ssid_t& ssid) override {}  // no-op
  void ReceiveNotification(void* payload) override;

 private:
  bool rx_enabled_ = false;
  uint8_t channel_;
  simulation::Environment* env_;
  EventHandlers event_handlers_;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_HW_H_
