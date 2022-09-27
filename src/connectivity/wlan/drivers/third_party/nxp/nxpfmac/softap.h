// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_SOFTAP_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_SOFTAP_H_

#include <fidl/fuchsia.wlan.ieee80211/cpp/common_types.h>
#include <fuchsia/hardware/wlan/fullmac/cpp/banjo.h>
#include <netinet/if_ether.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <mutex>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/event_handler.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_request.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan/mlan_ieee.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/waitable_state.h"

namespace wlan::nxpfmac {

struct DeviceContext;

class SoftAp {
 public:
  using StatusCode = fuchsia_wlan_ieee80211::StatusCode;

  SoftAp(DeviceContext* context, uint32_t bss_index);
  ~SoftAp();
  // Attempt to start the SoftAP on the given bss and channel. Returns ZX_ERR_ALREADY_EXISTS if a
  // SoftAP has already been started. Returns ZX_OK if the start request is successful.
  wlan_start_result_t Start(const wlan_fullmac_start_req* req) __TA_EXCLUDES(mutex_);

  // Returns ZX_ERR_ if not started or ZX_OK if the request is successful.
  wlan_stop_result_t Stop(const wlan_fullmac_stop_req* req) __TA_EXCLUDES(mutex_);

 private:
  // SoftApIfc* ifc_ = nullptr;
  DeviceContext* context_ = nullptr;
  const uint32_t bss_index_;
  cssid_t ssid_ = {};
  bool started_ __TA_GUARDED(mutex_) = false;
  std::mutex mutex_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_SOFTAP_H_
