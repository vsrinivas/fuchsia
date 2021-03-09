/*
 * Copyright (c) 2021 The Fuchsia Authors
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

#include <atomic>
#include <cstdint>

namespace wlan::brcmfmac {

template <typename Throttler>
class ThrottleCounter {
 public:
  explicit ThrottleCounter(Throttler& throttler) : throttler_(throttler) {}

  // Attempt to consume a token from the token bucket to use for logging. If the consume is
  // successful the previous number of throttled events is placed in |counter|. If the consume is
  // not successful the number of throttled events, INCLUDING this one, is placed in |counter|.
  // The internal counter is reset on each successful consume so the next call to consume will
  // either set |counter| to 0 (on success) or 1 (on failure).
  bool consume(uint64_t* out_counter) {
    if (throttler_.consume()) {
      // Clear the counter and fetch the previous value atomically, this ensures that in the case
      // of multiple consumes in parallel only one of them will report multiple throttled events.
      *out_counter = counter_.exchange(0, std::memory_order_relaxed);
      return true;
    }
    // fetch_add returns the old value so add 1
    *out_counter = counter_.fetch_add(1, std::memory_order_relaxed) + 1;
    return false;
  }

 private:
  Throttler& throttler_;
  std::atomic<uint64_t> counter_ = 0;
};

}  // namespace wlan::brcmfmac
