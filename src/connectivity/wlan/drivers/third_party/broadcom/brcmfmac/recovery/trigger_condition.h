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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_RECOVERY_TRIGGER_CONDITION_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_RECOVERY_TRIGGER_CONDITION_H_

#include <zircon/status.h>

#include <atomic>
#include <functional>
#include <mutex>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

namespace wlan::brcmfmac {

class TriggerCondition {
 public:
  TriggerCondition(uint32_t threshold, std::shared_ptr<std::function<zx_status_t()>> callback);
  ~TriggerCondition();

  // Increase the counter by 1.
  zx_status_t Inc();
  // Set the counter of this trigger condition to 0.
  void Clear();

 private:
  // The calls to the functions in this class could from multiple threads, add a lock to make it
  // thread safe.
  std::mutex trigger_lock_;

  const uint32_t threshold_;      // The boundary to trigger the recovery process when reached.
  std::atomic_uint32_t counter_;  // The counter of this condition.

  // The boolean that marks the counter already exceeds the threshold. It prevent the recovery
  // process from being triggered by one TriggerCondition for more than one time.
  std::atomic_bool over_threshold_;

  // The entry point of the recovery process.
  const std::shared_ptr<std::function<zx_status_t()>> recovery_start_callback_;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_RECOVERY_TRIGGER_CONDITION_H_
