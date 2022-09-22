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

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ies.h"

namespace wlan::nxpfmac {

// Return the actual size of a range of IEs.
static size_t determine_total_ie_size(const uint8_t* const ies, size_t size) {
  const uint8_t* const end = ies + size;
  const uint8_t* next = nullptr;
  for (const uint8_t* data = ies; data && data + Ie::kHdrSize < end; data = next) {
    next = data + data[Ie::kSizeOffset] + Ie::kHdrSize;
    if (next > end) {
      // This IE extends beyond the end of available data, consider it the end marker.
      return data - ies;
    }
  }
  // If ies was nullptr here this will still return 0, which is correct. Otherwise next should point
  // to the end of the sequence and this will return the whole length.
  return next - ies;
}

bool is_valid_ie_range(const uint8_t* ies, size_t size) {
  return determine_total_ie_size(ies, size) == size;
}

IeView::IeView(const uint8_t* ies, size_t length)
    : ies_(ies), length_(determine_total_ie_size(ies, length)) {}

std::optional<Ie> IeView::get(uint8_t ie_type) const {
  for (auto& ie : *this) {
    if (ie.type() == ie_type) {
      return std::make_optional<Ie>(ie);
    }
  }
  return std::nullopt;
}

}  // namespace wlan::nxpfmac
