// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_wlan_mlme as fidl_mlme, fidl_fuchsia_wlan_stats as fidl_stats};

pub fn empty_counter() -> fidl_stats::PacketCounter {
    fidl_stats::PacketCounter {
        in_: fidl_stats::Counter { count: 0, name: "".to_string() },
        out: fidl_stats::Counter { count: 0, name: "".to_string() },
        drop: fidl_stats::Counter { count: 0, name: "".to_string() },
        in_bytes: fidl_stats::Counter { count: 0, name: "".to_string() },
        out_bytes: fidl_stats::Counter { count: 0, name: "".to_string() },
        drop_bytes: fidl_stats::Counter { count: 0, name: "".to_string() },
    }
}

pub fn empty_stats_query_response() -> fidl_mlme::StatsQueryResponse {
    fidl_mlme::StatsQueryResponse {
        stats: fidl_stats::IfaceStats {
            dispatcher_stats: fidl_stats::DispatcherStats {
                any_packet: empty_counter(),
                mgmt_frame: empty_counter(),
                ctrl_frame: empty_counter(),
                data_frame: empty_counter(),
            },
            mlme_stats: None,
        },
    }
}
