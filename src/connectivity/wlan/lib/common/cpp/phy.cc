// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/phy.h>

#include <cstdio>

namespace wlan {
namespace common {

std::string Alpha2ToStr(fbl::Span<const uint8_t> alpha2) {
    if (alpha2.size() != WLANPHY_ALPHA2_LEN) {
        return "Invalid alpha2 length";
    }
    char buf[WLANPHY_ALPHA2_LEN * 8 + 1];
    auto data = alpha2.data();
    bool is_printable = std::isprint(data[0]) && std::isprint(data[1]);
    if (is_printable) {
        snprintf(buf, sizeof(buf), "%c%c", data[0], data[1]);
    } else {
        snprintf(buf, sizeof(buf), "(%u)(%u)", data[0], data[1]);
    }
    return std::string(buf);
}

}  // namespace common
}  // namespace wlan
