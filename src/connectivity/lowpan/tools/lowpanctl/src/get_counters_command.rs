// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{Context as _, Error};
use argh::FromArgs;

/// Contains the arguments decoded for the `get-counters` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get-counters")]
pub struct GetCountersCommand {
    /// reset counters
    #[argh(switch)]
    pub reset: bool,
}

macro_rules! print_one_counter {
    ($prefix:expr, $name:expr, $e:expr) => {
        if let Some(value) = $e.as_ref() {
            println!("{}.{} = {}", $prefix, $name, value);
        }
    };
}

macro_rules! print_mac_counters {
    ($prefix:expr, $table:expr) => {
        let table: fidl_fuchsia_lowpan_device::MacCounters = $table;
        print_one_counter!($prefix, "total", table.total);
        print_one_counter!($prefix, "unicast", table.unicast);
        print_one_counter!($prefix, "broadcast", table.broadcast);
        print_one_counter!($prefix, "ack_requested", table.ack_requested);
        print_one_counter!($prefix, "acked", table.acked);
        print_one_counter!($prefix, "no_ack_requested", table.no_ack_requested);
        print_one_counter!($prefix, "data", table.data);
        print_one_counter!($prefix, "data_poll", table.data_poll);
        print_one_counter!($prefix, "beacon", table.beacon);
        print_one_counter!($prefix, "beacon_request", table.beacon_request);
        print_one_counter!($prefix, "other", table.other);
        print_one_counter!($prefix, "address_filtered", table.address_filtered);
        print_one_counter!($prefix, "retries", table.retries);
        print_one_counter!($prefix, "direct_max_retry_expiry", table.direct_max_retry_expiry);
        print_one_counter!($prefix, "indirect_max_retry_expiry", table.indirect_max_retry_expiry);
        print_one_counter!($prefix, "dest_addr_filtered", table.dest_addr_filtered);
        print_one_counter!($prefix, "duplicated", table.duplicated);
        print_one_counter!($prefix, "err_no_frame", table.err_no_frame);
        print_one_counter!($prefix, "err_unknown_neighbor", table.err_unknown_neighbor);
        print_one_counter!($prefix, "err_invalid_src_addr", table.err_invalid_src_addr);
        print_one_counter!($prefix, "err_sec", table.err_sec);
        print_one_counter!($prefix, "err_fcs", table.err_fcs);
        print_one_counter!($prefix, "err_cca", table.err_cca);
        print_one_counter!($prefix, "err_abort", table.err_abort);
        print_one_counter!($prefix, "err_busy_channel", table.err_busy_channel);
        print_one_counter!($prefix, "err_other", table.err_other);
    };
}

macro_rules! print_coex_counters {
    ($prefix:expr, $table:expr) => {
        let table: fidl_fuchsia_lowpan_device::CoexCounters = $table;
        print_one_counter!($prefix, "requests", table.requests);
        print_one_counter!($prefix, "grant_immediate", table.grant_immediate);
        print_one_counter!($prefix, "grant_wait", table.grant_wait);
        print_one_counter!($prefix, "grant_wait_activated", table.grant_wait_activated);
        print_one_counter!($prefix, "grant_wait_timeout", table.grant_wait_timeout);
        print_one_counter!(
            $prefix,
            "grant_deactivated_during_request",
            table.grant_deactivated_during_request
        );
        print_one_counter!($prefix, "delayed_grant", table.delayed_grant);
        print_one_counter!(
            $prefix,
            "avg_delay_request_to_grant_usec",
            table.avg_delay_request_to_grant_usec
        );
        print_one_counter!($prefix, "grant_none", table.grant_none);
    };
}

impl GetCountersCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let counters_proxy =
            context.get_default_device_counters().await.context("Unable to get device instance")?;

        let result =
            if self.reset { counters_proxy.reset().await? } else { counters_proxy.get().await? };

        if let Some(table) = result.mac_tx {
            print_mac_counters!("mac.tx", table);
        }

        if let Some(table) = result.mac_rx {
            print_mac_counters!("mac.rx", table);
        }

        if let Some(table) = result.coex_tx {
            print_coex_counters!("coex.tx", table);
        }

        if let Some(table) = result.coex_rx {
            print_coex_counters!("coex.rx", table);
        }

        if let Some(value) = result.coex_saturated {
            println!("coex.saturated = {}", value);
        }

        Ok(())
    }
}
