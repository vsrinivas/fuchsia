// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/parse_mac_header.h>

namespace wlan {
namespace common {

std::optional<ParsedDataFrameHeader> ParseDataFrameHeader(BufferReader* r) {
    ParsedDataFrameHeader ret = {};

    ret.fixed = r->Read<DataFrameHeader>();
    if (ret.fixed == nullptr) { return {}; }

    if (ret.fixed->fc.to_ds() == 1 && ret.fixed->fc.from_ds() == 1) {
        ret.addr4 = r->Read<MacAddr>();
        if (ret.addr4 == nullptr) { return {}; }
    }

    if ((ret.fixed->fc.subtype() & DataSubtypeBitmask::kBitmaskQos) != 0) {
        ret.qos_ctrl = r->Read<QosControl>();
        if (ret.qos_ctrl == nullptr) { return {}; }
    }

    if (ret.fixed->fc.HasHtCtrl()) {
        ret.ht_ctrl = r->Read<HtControl>();
        if (ret.ht_ctrl == nullptr) { return {}; }
    }

    return { ret };
}

} // namespace common
} // namespace wlan
