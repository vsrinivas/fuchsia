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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_EVENT_HANDLER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_EVENT_HANDLER_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <functional>
#include <mutex>
#include <optional>
#include <vector>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan.h"

namespace wlan::nxpfmac {

class EventHandler {
 public:
  using EventCallback = std::function<void(pmlan_event)>;

  // Register for an event and receive callbacks for all events regardless of interface (if any).
  void RegisterForEvent(mlan_event_id event_id, EventCallback&& callback);
  // Register only for events for a specific interface identified by bss_idx.
  void RegisterForInterfaceEvent(mlan_event_id event_id, uint32_t bss_idx,
                                 EventCallback&& callback);

  void OnEvent(pmlan_event event);

 private:
  struct Callback {
    std::optional<uint32_t> bss_idx;
    EventCallback callback;
  };
  using CallbackStorage = std::vector<Callback>;
  std::unordered_map<mlan_event_id, CallbackStorage> callbacks_ __TA_GUARDED(mutex_);
  std::mutex mutex_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_EVENT_HANDLER_H_
