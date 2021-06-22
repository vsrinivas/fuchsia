// Copyright (c) 2021 The Fuchsia Authors
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

#include "recovery_trigger.h"

namespace wlan::brcmfmac {

TriggerCondition::TriggerCondition(uint32_t threshold,
                                   std::shared_ptr<std::function<zx_status_t()>> callback)
    : threshold_(threshold),
      counter_(0),
      over_threshold_(false),
      recovery_start_callback_(callback) {}

TriggerCondition::~TriggerCondition() = default;

zx_status_t TriggerCondition::Inc() {
  std::lock_guard<std::mutex> lock(trigger_lock_);
  zx_status_t error = ZX_OK;

  BRCMF_WARN("Caught an error instance, increasing TriggerCondition counter.");

  if (++counter_ >= threshold_) {
    // Check whether the callback function has value first.
    if (!recovery_start_callback_) {
      BRCMF_ERR("No recovery start callback funtion has been registered.");
      return ZX_ERR_NOT_FOUND;
    }

    bool expected = false;
    // Test and set the bool to true if there is a callback function, because it should be cleared
    // inside the callback function.
    if (!over_threshold_.compare_exchange_strong(expected, true)) {
      BRCMF_INFO("Recovery already triggered - ignoring additional trigger");
      return ZX_ERR_BAD_STATE;
    }

    if ((error = (*recovery_start_callback_)()) != ZX_OK) {
      BRCMF_ERR("Schedule recovery worker error: %s", zx_status_get_string(error));
      return error;
    }
  }

  return ZX_OK;
}

void TriggerCondition::Clear() {
  std::lock_guard<std::mutex> lock(trigger_lock_);
  counter_.store(0);
  over_threshold_.store(false);
}

}  // namespace wlan::brcmfmac
