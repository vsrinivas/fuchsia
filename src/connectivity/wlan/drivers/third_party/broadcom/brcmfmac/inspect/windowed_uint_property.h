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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_INSPECT_WINDOWED_UINT_PROPERTY_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_INSPECT_WINDOWED_UINT_PROPERTY_H_

#include <lib/inspect/cpp/inspect.h>

#include <queue>

namespace wlan::brcmfmac {

// WindowedUintProperty class helps simplify the implementation of inspect counters that reflect
// the count of events over just the past N units of time.
//
// Such counters come in handy for 'Detect' usecases where snapshots need to be triggered based on
// thresholds.
//
// Init()
//    Lets user initialize an inspect counter that tracks event count over a specificed window of
// time. The 'window of time' is equal to window_size multiplied by the interval at which the user
// decides to move the window.
//
// Add()
//    Incrementes the inspect counter.
//
// SlideWindow()
//    Moves the window by one slot.
//
// Example Usage:
//
// a) User needs an inspect counter that tracks events across a 1hr period, refreshed ever minute
//    => window_size = 60 and user needs to invoke SlideWindow() ever minute.
//
// b) User needs a counter that tracks events across a 1day period, refreshed 30 minutes =>
//    window_size = 48 and and user needs to invoke SlideWindow() needs to be invoked every 30
//    minutes.

class WindowedUintProperty {
 public:
  zx_status_t Init(inspect::Node* p, uint32_t window_size, const std::string& name, uint64_t value);

  // We do not support Set() and Subtract() to avoid the possibility of the counter becoming
  // negative for a window of time.
  void Add(uint64_t value);
  void SlideWindow();

 private:
  std::mutex lock_;  // Protects internal counters and queue.
  uint64_t count_;   // Free rolling counter of event count.
  uint32_t queue_capacity_;
  std::queue<uint64_t> count_queue_;
  inspect::UintProperty icount_;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_INSPECT_WINDOWED_UINT_PROPERTY_H_
