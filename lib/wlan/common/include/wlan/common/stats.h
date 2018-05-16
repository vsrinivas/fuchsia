// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan_stats/c/fidl.h>

#include <atomic>
#include <string>

namespace wlan {

constexpr bool kStatsDebugEnabled = false;

#define WLAN_STATS_ADD(i, v)                                                        \
    do {                                                                            \
        stats_.stats.v.Inc(i);                                                      \
        if (kStatsDebugEnabled) {                                                   \
            if (stats_.stats.v.name.empty()) {                                      \
                stats_.stats.v.name = __PRETTY_FUNCTION__;                          \
                debugf("Enabled statistics for %s\n", stats_.stats.v.name.c_str()); \
            }                                                                       \
        }                                                                           \
    } while (0)

#define WLAN_STATS_INC(v) WLAN_STATS_ADD(1UL, v)

namespace common {

struct Counter {
    std::atomic_uint64_t count{0};
    std::string name;  // Dynamically set at run-time
    const wlan_stats_Counter ToFidl() const {
        return wlan_stats_Counter{
            .count = count.load(),
            .name = fidl_string_t{.size = name.length(), .data = strdup(name.c_str())}};
    };
    uint64_t Inc(uint64_t i) { return count.fetch_add(i, std::memory_order_relaxed); }
};

struct PacketCounter {
    Counter in;
    Counter out;
    Counter drop;
    const wlan_stats_PacketCounter ToFidl() const {
        return wlan_stats_PacketCounter{
            .in = in.ToFidl(), .out = out.ToFidl(), .drop = drop.ToFidl()};
    };
};

// LINT.IfChange
struct DispatcherStats {
    PacketCounter any_packet;
    PacketCounter mgmt_frame;
    PacketCounter ctrl_frame;
    PacketCounter data_frame;
    const wlan_stats_DispatcherStats ToFidl() const {
        return wlan_stats_DispatcherStats{.any_packet = any_packet.ToFidl(),
                                          .mgmt_frame = mgmt_frame.ToFidl(),
                                          .ctrl_frame = ctrl_frame.ToFidl(),
                                          .data_frame = data_frame.ToFidl()};
    };
};

struct ClientMlmeStats {
    PacketCounter svc_msg;
    PacketCounter data_frame;
    PacketCounter mgmt_frame;
    const wlan_stats_ClientMlmeStats ToFidl() const {
        return wlan_stats_ClientMlmeStats{.svc_msg = svc_msg.ToFidl(),
                                          .data_frame = data_frame.ToFidl(),
                                          .mgmt_frame = mgmt_frame.ToFidl()};
    };
};
// LINT.ThenChange(//garnet/public/lib/wlan/fidl/wlan_stats.fidl)

template <typename T, typename U> class WlanStats {
   public:
    T stats;
    const U ToFidl() const { return stats.toFidl(); };
};

}  // namespace common
}  // namespace wlan
