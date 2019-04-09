// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_STATS_H_
#define GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_STATS_H_

#include <fuchsia/wlan/stats/cpp/fidl.h>

#include <wlan/common/logging.h>
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

#define WLAN_RSSI_HIST_INC(s, r) stats_.stats.s.Inc(r, 1UL)

namespace common {

struct Counter {
    std::atomic_uint64_t count{0};
    std::string name;  // Dynamically set at run-time
    ::fuchsia::wlan::stats::Counter ToFidl() const {
        return ::fuchsia::wlan::stats::Counter{.count = count.load(std::memory_order_relaxed),
                                               .name = name};
    }
    void Reset() { count = 0; }
    uint64_t Inc(uint64_t i) { return count.fetch_add(i, std::memory_order_relaxed); }
};

struct PacketCounter {
    Counter in;
    Counter out;
    Counter drop;
    Counter in_bytes;
    Counter out_bytes;
    Counter drop_bytes;
    ::fuchsia::wlan::stats::PacketCounter ToFidl() const {
        return ::fuchsia::wlan::stats::PacketCounter{.in = in.ToFidl(),
                                                     .out = out.ToFidl(),
                                                     .drop = drop.ToFidl(),
                                                     .in_bytes = in_bytes.ToFidl(),
                                                     .out_bytes = out_bytes.ToFidl(),
                                                     .drop_bytes = drop_bytes.ToFidl()};
    }
    void Reset() {
        in.Reset();
        out.Reset();
        drop.Reset();
        in_bytes.Reset();
        out_bytes.Reset();
        drop_bytes.Reset();
    }
};

// LINT.IfChange
struct DispatcherStats {
    PacketCounter any_packet;
    PacketCounter mgmt_frame;
    PacketCounter ctrl_frame;
    PacketCounter data_frame;
    ::fuchsia::wlan::stats::DispatcherStats ToFidl() const {
        return ::fuchsia::wlan::stats::DispatcherStats{.any_packet = any_packet.ToFidl(),
                                                       .mgmt_frame = mgmt_frame.ToFidl(),
                                                       .ctrl_frame = ctrl_frame.ToFidl(),
                                                       .data_frame = data_frame.ToFidl()};
    }
    void Reset() {
        any_packet.Reset();
        mgmt_frame.Reset();
        ctrl_frame.Reset();
        data_frame.Reset();
    }
};

struct RssiStats {
    RssiStats() { std::fill(std::begin(hist), std::end(hist), 0); }
    ::fuchsia::wlan::stats::RssiStats ToFidl() const {
        std::lock_guard<std::mutex> guard(lock);
        ::fuchsia::wlan::stats::RssiStats rssi_stats{};
        rssi_stats.hist =
            std::vector<uint64_t>(hist, hist + ::fuchsia::wlan::stats::RSSI_BINS);
        return rssi_stats;
    }
    void Reset() {
        std::lock_guard<std::mutex> guard(lock);
        std::fill(std::begin(hist), std::end(hist), 0);
    }
    uint64_t Inc(const int8_t r, const uint64_t delta) {
        if (r > 0 || -r >= ::fuchsia::wlan::stats::RSSI_BINS) { return 0; }
        std::lock_guard<std::mutex> guard(lock);
        return hist[-r] += delta;
    }
    uint64_t Get(const int8_t r) {
        if (r > 0 || -r >= ::fuchsia::wlan::stats::RSSI_BINS) { return 0; }
        std::lock_guard<std::mutex> guard(lock);
        return hist[-r];
    }

   private:
    uint64_t hist[::fuchsia::wlan::stats::RSSI_BINS] __TA_GUARDED(lock);
    mutable std::mutex lock;
};

struct ClientMlmeStats {
    PacketCounter svc_msg;
    PacketCounter data_frame;
    PacketCounter mgmt_frame;
    PacketCounter tx_frame;
    PacketCounter rx_frame;
    RssiStats assoc_data_rssi;
    RssiStats beacon_rssi;
    ::fuchsia::wlan::stats::ClientMlmeStats ToFidl() const {
        return ::fuchsia::wlan::stats::ClientMlmeStats{.svc_msg = svc_msg.ToFidl(),
                                                       .data_frame = data_frame.ToFidl(),
                                                       .mgmt_frame = mgmt_frame.ToFidl(),
                                                       .tx_frame = tx_frame.ToFidl(),
                                                       .rx_frame = rx_frame.ToFidl(),
                                                       .assoc_data_rssi = assoc_data_rssi.ToFidl(),
                                                       .beacon_rssi = beacon_rssi.ToFidl()};
    }
    void Reset() {
        svc_msg.Reset();
        data_frame.Reset();
        mgmt_frame.Reset();
        tx_frame.Reset();
        rx_frame.Reset();
        assoc_data_rssi.Reset();
        beacon_rssi.Reset();
    }
};
// LINT.ThenChange(//garnet/public/lib/wlan/fidl/wlan_stats.fidl)

template <typename T, typename U> class WlanStats {
   public:
    T stats;
    U ToFidl() const { return stats.ToFidl(); }
    void Reset() { stats.Reset(); }
};

}  // namespace common
}  // namespace wlan

#endif  // GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_STATS_H_
