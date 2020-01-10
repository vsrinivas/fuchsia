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

using RxHandler = std::function<void(const simulation::SimFrame* frame)>;

class SimHardware : public simulation::StationIfc {
 public:
  struct EventHandlers {
    // Now it only have one member but might add event other than receive in the future.
    RxHandler rx_handler;
  };

  explicit SimHardware(simulation::Environment* env);

  // Tells us how to call the SimFirmware instance
  void SetCallbacks(const EventHandlers& handlers);

  void EnableRx() { rx_enabled_ = true; }
  void DisableRx() { rx_enabled_ = false; }

  void SetChannel(wlan_channel_t channel) { channel_ = channel; }

  void GetRevInfo(brcmf_rev_info_le* rev_info);

  void RequestCallback(std::function<void()>* callback, zx::duration delay,
                       uint64_t* id_out = nullptr);
  void CancelCallback(uint64_t id);

  // StationIfc methods
  void Rx(const simulation::SimFrame* frame) override;
  void ReceiveNotification(void* payload) override;

  // Operations that are forwarded to the environment
  void Tx(const simulation::SimFrame* frame);

 private:
  bool rx_enabled_ = false;
  wlan_channel_t channel_;
  simulation::Environment* env_;
  EventHandlers event_handlers_;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_HW_H_
