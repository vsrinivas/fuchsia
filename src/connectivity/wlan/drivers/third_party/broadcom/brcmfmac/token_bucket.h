// Copyright (c) 2020 The Fuchsia Authors
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
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TOKEN_BUCKET_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TOKEN_BUCKET_H_

#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <atomic>

namespace wlan::brcmfmac {

// A token bucket that issues tokens at a fixed rate specified at construction. The rate of tokens
// is specified as tokens per seconds which is a double. This means that tokens can be issue at a
// rate lower than one token per second. The bucket has a capacity that is specified at
// construction. This causes tokens to build up to capacity and allow a burst of token to be issued
// in a short time before being limited by the token regeneration.
//
// In terms of implementation the bucket uses CPU ticks as currency to pay for tokens. The bucket
// accumulates currency as the CPU ticks. At construction the price (in ticks) for a token is
// computed and each time a token is requested the bucket will check to see if it has accumulated
// enough ticks since the last token was issued. If it has enough ticks the bucket will add the
// price of the token to the time the last token was issued, thereby consuming ticks.
class TokenBucket {
  public:
    explicit TokenBucket(double tokens_per_second, zx_ticks_t capacity = 1)
      : capacity_(capacity)
      , ticks_per_token_(static_cast<zx_ticks_t>(
        static_cast<double>(zx_ticks_per_second()) / tokens_per_second))
      // Start out with a full bucket, i.e. the last issued tick was at capacity ticks in past
      , last_issued_tick_(zx_ticks_get() - capacity_ * ticks_per_token_) {
    }

    // Attempt to consume one token. If a token is successfully consumed then one token will be
    // deducted and true is returned. Returns false if there are not enough tokens.
    bool consume() {
      zx_ticks_t current_tick = zx_ticks_get();
      zx_ticks_t old_tick = last_issued_tick_.load(std::memory_order_relaxed);

      // If the last tick that we issued a token is further back than the capacity of the bucket we
      // need to update it to be full but not over capacity.
      zx_ticks_t min_tick = current_tick - ticks_per_token_ * capacity_;
      zx_ticks_t updated_tick = min_tick > old_tick ? min_tick : old_tick;

      while (true) {
        // Add the cost of a token to the time we last issued a token, if the total exceeds the
        // current number of ticks that is the same as the cost being too high.
        updated_tick += ticks_per_token_;
        if (updated_tick > current_tick) {
          // The number of ticks required to consume a token is too high
          return false;
        }
        if (last_issued_tick_.compare_exchange_weak(old_tick, updated_tick,
                                                    std::memory_order_relaxed)) {
          // lastIssuedTick_ didn't change and now contains updatedTick
          return true;
        }
        // lastIssuedTick_ changed, updated value is in old_tick, check again
        updated_tick = old_tick;
      }
    }

    const zx_ticks_t capacity_;
    const zx_ticks_t ticks_per_token_;
    std::atomic<zx_ticks_t> last_issued_tick_;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TOKEN_BUCKET_H_
