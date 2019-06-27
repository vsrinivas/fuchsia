// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PHY_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PHY_H_

#include <ddk/protocol/wlanphyimpl.h>
#include <fbl/span.h>

#include <array>
#include <string>

namespace wlan {
namespace common {

std::string Alpha2ToStr(fbl::Span<const uint8_t> alpha2);

}  // namespace common
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PHY_H_
