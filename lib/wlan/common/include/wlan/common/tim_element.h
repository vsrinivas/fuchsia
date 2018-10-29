// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_TIM_H_
#define GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_TIM_H_

#include <wlan/common/parse_element.h>

namespace wlan {
namespace common {

bool IsTrafficBuffered(uint16_t aid, const TimHeader& tim_hdr, Span<const uint8_t> bitmap);

std::optional<ParsedTim> FindAndParseTim(Span<const uint8_t> ies);

} // namespace common
} // namespace wlan

#endif // GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_TIM_H_
