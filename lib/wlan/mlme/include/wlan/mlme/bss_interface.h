// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/device_interface.h>

#include <wlan/common/macaddr.h>
#include <zircon/types.h>

#include <chrono>

namespace wlan {

// TODO(hahnr): Replace with uint64_t.
namespace bss {
using timestamp_t = std::chrono::time_point<std::chrono::steady_clock, std::chrono::nanoseconds>;
}
using aid_t = size_t;

class BssInterface {
   public:
    virtual const common::MacAddr& bssid() const = 0;
    virtual uint64_t timestamp() = 0;
    virtual zx_status_t AssignAid(const common::MacAddr& client, aid_t* out_aid) = 0;
    virtual zx_status_t ReleaseAid(const common::MacAddr& client) = 0;
};

}  // namespace wlan