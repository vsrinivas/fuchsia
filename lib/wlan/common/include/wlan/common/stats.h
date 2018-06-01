// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/wlan/stats/cpp/fidl.h>

#include <atomic>
#include <string>

namespace wlan {

namespace wlan_stats = ::fuchsia::wlan::stats;

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
    const wlan_stats::Counter ToFidl() const {
        return wlan_stats::Counter{
            .count = count.load(),
            .name = name};
    };
    uint64_t Inc(uint64_t i) { return count.fetch_add(i, std::memory_order_relaxed); }
};

struct PacketCounter {
    Counter in;
    Counter out;
    Counter drop;
    const wlan_stats::PacketCounter ToFidl() const {
        return wlan_stats::PacketCounter{
            .in = in.ToFidl(), .out = out.ToFidl(), .drop = drop.ToFidl()};
    };
};

// LINT.IfChange
struct DispatcherStats {
    PacketCounter any_packet;
    PacketCounter mgmt_frame;
    PacketCounter ctrl_frame;
    PacketCounter data_frame;
    wlan_stats::DispatcherStats ToFidl() const {
        return wlan_stats::DispatcherStats{.any_packet = any_packet.ToFidl(),
                                          .mgmt_frame = mgmt_frame.ToFidl(),
                                          .ctrl_frame = ctrl_frame.ToFidl(),
                                          .data_frame = data_frame.ToFidl()};
    };
};

struct ClientMlmeStats {
    PacketCounter svc_msg;
    PacketCounter data_frame;
    PacketCounter mgmt_frame;
    wlan_stats::ClientMlmeStats ToFidl() const {
        return wlan_stats::ClientMlmeStats{.svc_msg = svc_msg.ToFidl(),
                                          .data_frame = data_frame.ToFidl(),
                                          .mgmt_frame = mgmt_frame.ToFidl()};
    };
};
// LINT.ThenChange(//garnet/public/lib/wlan/fidl/wlan_stats.fidl)

template <typename T, typename U> class WlanStats {
   public:
    T stats;
    U ToFidl() const { return stats.ToFidl(); };
};

}  // namespace common
}  // namespace wlan
