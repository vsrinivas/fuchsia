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
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan.h"

namespace wlan::nxpfmac {

class EventHandler;

// A class that represents a registration of an event. This can be used to unregister the event with
// the event handler. The event will automatically be unregistered when the event registration
// object is destroyed. This means that the caller HAS to keep this object alive for as long as they
// wish to receive the event. The object is marked as nodiscard to help enforce this.
class [[nodiscard]] EventRegistration {
 public:
  EventRegistration() = default;
  // Copying dis-allowed as this would imply registering twice.
  EventRegistration(const EventRegistration&) = delete;
  EventRegistration& operator=(const EventRegistration&) = delete;
  // Moving allowed.
  EventRegistration(EventRegistration&& other) noexcept;
  // Note that moving to an object will cause the destination object to be unregistered if it was
  // registered.
  EventRegistration& operator=(EventRegistration&& other) noexcept;

  ~EventRegistration();

 private:
  friend class EventHandler;
  EventRegistration(EventHandler* event_handler, mlan_event_id event_id, void* event);

  EventHandler* event_handler_ = nullptr;
  mlan_event_id event_id_ = MLAN_EVENT_ID_FW_UNKNOWN;
  void* event_ = nullptr;
};

class EventHandler {
 public:
  using EventCallback = std::function<void(pmlan_event)>;

  // Register for an event and receive callbacks for all events regardless of interface (if any).
  EventRegistration RegisterForEvent(mlan_event_id event_id, EventCallback&& callback);
  // Register only for events for a specific interface identified by bss_idx.
  EventRegistration RegisterForInterfaceEvent(mlan_event_id event_id, uint32_t bss_idx,
                                              EventCallback&& callback);
  // Unregister a previously registered event. Returns true if the event was unregistered
  // successfully, false if the event registration could not be found (e.g. it was already
  // unregistered).
  bool UnregisterEvent(EventRegistration&& event_registration);

  void OnEvent(pmlan_event event);

 private:
  struct Callback {
    explicit Callback(EventCallback&& callback) : callback(callback) {}
    Callback(uint32_t bss_idx, EventCallback&& callback) : bss_idx(bss_idx), callback(callback) {}
    std::optional<uint32_t> bss_idx;
    EventCallback callback;
  };
  using CallbackStorage = std::vector<std::unique_ptr<Callback>>;
  std::unordered_map<mlan_event_id, CallbackStorage> callbacks_ __TA_GUARDED(mutex_);
  std::mutex mutex_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_EVENT_HANDLER_H_
