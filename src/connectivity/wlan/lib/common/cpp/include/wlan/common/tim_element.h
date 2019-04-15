// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_TIM_ELEMENT_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_TIM_ELEMENT_H_

#include <wlan/common/parse_element.h>

namespace wlan {
namespace common {

bool IsTrafficBuffered(uint16_t aid, const TimHeader& tim_hdr,
                       Span<const uint8_t> bitmap);

std::optional<ParsedTim> FindAndParseTim(Span<const uint8_t> ies);

}  // namespace common
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_TIM_ELEMENT_H_
