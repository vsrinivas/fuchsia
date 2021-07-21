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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_hw.h"

#include <fuchsia/wlan/common/c/banjo.h>

#include <cstring>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcm_hw_ids.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_d11.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

namespace wlan::brcmfmac {

SimHardware::SimHardware(std::shared_ptr<simulation::Environment> env) : env_(env) {
  env->AddStation(this);
}

SimHardware::~SimHardware() {
  // Clean all the events the firmware scheduled.
  for (uint64_t id : scheduled_ids) {
    // Directly calls CancelNotification() in sim-env because the ids in the list don't have
    // to be removed right before the release of sim-hw.
    env_->CancelNotification(id);
  }
  env_->RemoveStation(this);
}

void SimHardware::SetCallbacks(const EventHandlers& handlers) { event_handlers_ = handlers; }

static bool ChannelsMatch(const wlan_channel_t& c1, const wlan_channel_t& c2) {
  return (c1.primary == c2.primary) && (c1.cbw == c2.cbw) && (c1.secondary80 == c2.secondary80);
}

void SimHardware::Rx(std::shared_ptr<const simulation::SimFrame> frame,
                     std::shared_ptr<const simulation::WlanRxInfo> info) {
  if (!rx_enabled_ || !ChannelsMatch(info->channel, channel_))
    return;
  // Simply transfer frame to firmware.
  event_handlers_.rx_handler(frame, info);
}

void SimHardware::GetRevInfo(brcmf_rev_info_le* rev_info) {
  // Settings were copied from traces on a VIM2
  rev_info->vendorid = BRCM_PCIE_VENDOR_ID_BROADCOM;
  rev_info->deviceid = BRCM_PCIE_4350_DEVICE_ID;
  rev_info->radiorev = 0x292069;
  rev_info->chiprev = 2;
  rev_info->corerev = 48;
  rev_info->boardid = 0x73e;
  rev_info->boardvendor = BRCM_PCIE_VENDOR_ID_BROADCOM;
  rev_info->boardrev = 0x1121;
  rev_info->driverrev = 0x7234f00;
  rev_info->ucoderev = 0;
  rev_info->bus = 0;
  rev_info->chipnum = BRCM_CC_4356_CHIP_ID;
  rev_info->phytype = 0xb;
  rev_info->phyrev = 0x11;
  rev_info->anarev = 0;
  rev_info->chippkg = 2;
  rev_info->nvramrev = 0x5b2b4;
}

void SimHardware::RequestCallback(std::function<void()> callback, zx::duration delay,
                                  uint64_t* id_out) {
  // Always store the event id scheduled by the simulated firmware.
  uint64_t tmp_id = 0;
  if (env_->ScheduleNotification(std::move(callback), delay, &tmp_id) != ZX_OK) {
    BRCMF_ERR("Fail to schedule event from sim-hw.");
    return;
  }

  // Store the id if we get an value from sim-env.
  scheduled_ids.push_back(tmp_id);

  // Report back the id if firmware needs.
  if (id_out != nullptr) {
    *id_out = tmp_id;
  }
}

void SimHardware::CancelCallback(uint64_t id) {
  if (env_->CancelNotification(id) != ZX_OK) {
    BRCMF_ERR("Event has already been cancelled or executed.");
    return;
  }
  // Remove event from the list if it's cancelled successfully, but the those event who have already
  // been executed will still remain in the list, and sim-fw will still try to cancel them in
  // destructor.
  scheduled_ids.remove(id);
}

void SimHardware::Tx(const simulation::SimFrame& frame) {
  simulation::WlanTxInfo info = {.channel = channel_};
  env_->Tx(frame, info, this);
}

}  // namespace wlan::brcmfmac
