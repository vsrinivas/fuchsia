// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{Context as _, Error};
use argh::FromArgs;
use fidl_fuchsia_lowpan_device::MacCounters;

/// Contains the arguments decoded for the `get-counters` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get-counters")]
pub struct GetCountersCommand {
    /// reset counters
    #[argh(switch)]
    pub reset: bool,
}

macro_rules! get_fmt_u32_str {
    ($c:ident, $i:ident) => {
        format!("{:^16}", $c.$i.map(|x| x.to_string()).unwrap_or("None".to_string()))
    };
}

fn format_counter_name(name: &str) -> String {
    format!("{:^25}", name.to_string())
}

macro_rules! print_one_counter {
    ($nn:expr, $n:ident, $t:ident, $r:ident) => {
        println!("+---------------------------+------------------+------------------+");
        let counter_name = format_counter_name($nn);
        let tx_counter_val = get_fmt_u32_str!($t, $n);
        let rx_counter_val = get_fmt_u32_str!($r, $n);
        println!("| {} | {} | {} |", counter_name, tx_counter_val, rx_counter_val);
    };
}

impl GetCountersCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let counters_proxy =
            context.get_default_device_counters().await.context("Unable to get device instance")?;

        let result =
            if self.reset { counters_proxy.reset().await? } else { counters_proxy.get().await? };

        println!("+---------------------------+------------------+------------------+");
        println!("|        Counter Name       |        Tx        |        Rx        |");
        let tx_counters = result.mac_tx.unwrap_or(MacCounters { ..MacCounters::EMPTY });
        let rx_counters = result.mac_rx.unwrap_or(MacCounters { ..MacCounters::EMPTY });

        print_one_counter!("total", total, tx_counters, rx_counters);
        print_one_counter!("unicast", unicast, tx_counters, rx_counters);
        print_one_counter!("broadcast", broadcast, tx_counters, rx_counters);
        print_one_counter!("ack_requested", ack_requested, tx_counters, rx_counters);
        print_one_counter!("acked", acked, tx_counters, rx_counters);
        print_one_counter!("no_ack_requested", no_ack_requested, tx_counters, rx_counters);
        print_one_counter!("data", data, tx_counters, rx_counters);
        print_one_counter!("data_poll", data_poll, tx_counters, rx_counters);
        print_one_counter!("beacon", beacon, tx_counters, rx_counters);
        print_one_counter!("beacon_request", beacon_request, tx_counters, rx_counters);
        print_one_counter!("other", other, tx_counters, rx_counters);
        print_one_counter!("address_filtered", address_filtered, tx_counters, rx_counters);
        print_one_counter!("retries", retries, tx_counters, rx_counters);
        print_one_counter!(
            "direct_max_retry_expiry",
            direct_max_retry_expiry,
            tx_counters,
            rx_counters
        );
        print_one_counter!(
            "indirect_max_retry_expiry",
            indirect_max_retry_expiry,
            tx_counters,
            rx_counters
        );
        print_one_counter!("dest_addr_filtered", dest_addr_filtered, tx_counters, rx_counters);
        print_one_counter!("duplicated", duplicated, tx_counters, rx_counters);
        print_one_counter!("err_no_frame", err_no_frame, tx_counters, rx_counters);
        print_one_counter!("err_unknown_neighbor", err_unknown_neighbor, tx_counters, rx_counters);
        print_one_counter!("err_invalid_src_addr", err_invalid_src_addr, tx_counters, rx_counters);
        print_one_counter!("err_sec", err_sec, tx_counters, rx_counters);
        print_one_counter!("err_fcs", err_fcs, tx_counters, rx_counters);
        print_one_counter!("err_cca", err_cca, tx_counters, rx_counters);
        print_one_counter!("err_abort", err_abort, tx_counters, rx_counters);
        print_one_counter!("err_busy_channel", err_busy_channel, tx_counters, rx_counters);
        print_one_counter!("err_other", err_other, tx_counters, rx_counters);

        println!("+---------------------------+------------------+------------------+");

        Ok(())
    }
}
