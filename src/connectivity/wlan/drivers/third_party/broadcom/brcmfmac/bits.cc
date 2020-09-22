// Copyright (c) 2019 The Fuchsia Authors.
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice appear
// in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bits.h"

#include <stdint.h>

// This is a kill-flies-with-sledgehammers, just-get-it-working version; TODO(fxbug.dev/29351) for
// efficiency.

bool brcmf_test_and_set_bit_in_array(size_t bit_number, std::atomic<unsigned long>* addr) {
  size_t index = bit_number >> 6;
  uint64_t bit = 1 << (bit_number & 0x3f);
  return !!(addr[index].fetch_or(bit) & bit);
}

bool brcmf_test_and_clear_bit_in_array(size_t bit_number, std::atomic<unsigned long>* addr) {
  uint32_t index = bit_number >> 6;
  uint64_t bit = 1 << (bit_number & 0x3f);
  return !!(addr[index].fetch_and(~bit) & bit);
}

bool brcmf_test_bit_in_array(size_t bit_number, std::atomic<unsigned long>* addr) {
  uint32_t index = bit_number >> 6;
  uint64_t bit = 1 << (bit_number & 0x3f);
  return !!(addr[index].load() & bit);
}

void brcmf_clear_bit_in_array(size_t bit_number, std::atomic<unsigned long>* addr) {
  (void)brcmf_test_and_clear_bit_in_array(bit_number, addr);
}

void brcmf_set_bit_in_array(size_t bit_number, std::atomic<unsigned long>* addr) {
  (void)brcmf_test_and_set_bit_in_array(bit_number, addr);
}
