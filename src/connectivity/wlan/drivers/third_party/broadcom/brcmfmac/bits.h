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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BITS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BITS_H_

#include <stddef.h>
#include <zircon/assert.h>

#include <atomic>

template <typename T>
constexpr uint64_t brcmf_bit(T bit_number) {
  uint8_t value = static_cast<std::underlying_type_t<T>>(bit_number);
  ZX_DEBUG_ASSERT(value < sizeof(unsigned long) * 8);
  return static_cast<uint64_t>(1 << value);
}

template <typename T>
inline bool brcmf_test_and_set_bit(T bit_number, std::atomic<unsigned long>* bit_array) {
  uint64_t b = brcmf_bit(bit_number);
  return !!(bit_array->fetch_or(b) & b);
}

template <typename T>
inline bool brcmf_test_and_clear_bit(T bit_number, std::atomic<unsigned long>* bit_array) {
  uint64_t b = brcmf_bit(bit_number);
  return !!(bit_array->fetch_and(~b) & b);
}

template <typename T>
inline bool brcmf_test_bit(T bit_number, std::atomic<unsigned long>* bit_array) {
  return !!(bit_array->load() & brcmf_bit(bit_number));
}

template <typename T>
inline bool brcmf_test_bit(T bit_number, unsigned long bit_array) {
  return !!(bit_array & brcmf_bit(bit_number));
}

template <typename T>
inline void brcmf_clear_bit(T bit_number, std::atomic<unsigned long>* bit_array) {
  (void)brcmf_test_and_clear_bit(bit_number, bit_array);
}

template <typename T>
inline void brcmf_set_bit(T bit_number, std::atomic<unsigned long>* bit_array) {
  (void)brcmf_test_and_set_bit(bit_number, bit_array);
}

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BITS_H_
