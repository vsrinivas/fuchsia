// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/element_splitter.h>
#include <wlan/common/tim_element.h>

namespace wlan {
namespace common {

// TODO(hahnr): Support dot11MultiBSSIDActivated is true.
bool IsTrafficBuffered(uint16_t aid, const TimHeader& tim_hdr,
                       Span<const uint8_t> bitmap) {
  size_t n1 = tim_hdr.bmp_ctrl.offset() * 2;

  size_t octet = aid / 8;
  // No traffic buffered for aid since it's not in the partial bitmap
  if (octet < n1 || octet - n1 >= bitmap.size())
    return false;

  // Traffic might be buffered for aid
  return bitmap[octet - n1] & (1 << (aid % 8));
}

std::optional<ParsedTim> FindAndParseTim(Span<const uint8_t> ies) {
  auto splitter = common::ElementSplitter{ies};
  auto it = std::find_if(splitter.begin(), splitter.end(), [](auto elem) {
    return std::get<element_id::ElementId>(elem) == element_id::kTim;
  });

  if (it == splitter.end()) {
    return {};
  }

  return common::ParseTim(std::get<1>(*it));
}

}  // namespace common
}  // namespace wlan
