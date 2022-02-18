// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use crate::prelude::*;

/// Contains the arguments decoded for the `get-mac-filter-settings` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get-neighbor-table")]
pub struct GetNeighborTableCommand {}

impl GetNeighborTableCommand {
    fn get_hex_string(&self, vec: &Vec<u8>) -> String {
        let mut string = String::from("");
        for item in vec {
            string.push_str(&format!("{:02X}", item)[..]);
        }
        string
    }

    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let (_, _, device_test_proxy) =
            context.get_default_device_proxies().await.context("Unable to get device instance")?;

        let result = device_test_proxy.get_neighbor_table().await?;

        println!("+------------------+------------+----------+---------+--------------+--------------+--------------+----------+--------+-------+");
        println!("|      MacAddr     |  ShortAddr |    Age   | IsChild | LinkFrameCnt | MgmtFrameCnt |  LastRssiIn  | AvgRssiIn|  LqiIn |  Mode |");

        for item in result {
            let mac_address = format!("{:^16}", self.get_hex_string(&item.mac_address.unwrap()));
            let short_address = format!(
                "{:^10}",
                item.short_address.map(|x| format!("0x{:04X}", x)).unwrap_or("".to_string())
            );

            let age = format!(
                "{:^8}",
                item.age
                    .map(|x| format!("{}s", fuchsia_zircon::Duration::from_nanos(x).into_seconds()))
                    .unwrap_or("".to_string())
            );
            let is_child =
                format!("{:^7}", item.is_child.map(|x| x.to_string()).unwrap_or("".to_string()));
            let link_frame_cnt = format!(
                "{:^12}",
                item.link_frame_count.map(|x| x.to_string()).unwrap_or("".to_string())
            );
            let mgmt_frame_cnt = format!(
                "{:^12}",
                item.mgmt_frame_count.map(|x| x.to_string()).unwrap_or("".to_string())
            );
            let last_rssi_in = format!(
                "{:^12}",
                item.last_rssi_in.map(|x| x.to_string()).unwrap_or("".to_string())
            );
            let avg_rssi_in =
                format!("{:^8}", item.avg_rssi_in.map(|x| x.to_string()).unwrap_or("".to_string()));
            let lqi_in =
                format!("{:^6}", item.lqi_in.map(|x| x.to_string()).unwrap_or("".to_string()));
            let mode = format!(
                "{:^5}",
                item.thread_mode.map(|x| format!("0x{:02X}", x)).unwrap_or("".to_string())
            );

            println!("+------------------+------------+----------+---------+--------------+--------------+--------------+----------+--------+-------+");
            println!(
                "| {} | {} | {} | {} | {} | {} | {} | {} | {} | {} |",
                mac_address,
                short_address,
                age,
                is_child,
                link_frame_cnt,
                mgmt_frame_cnt,
                last_rssi_in,
                avg_rssi_in,
                lqi_in,
                mode
            );
        }
        println!("+------------------+------------+----------+---------+--------------+--------------+--------------+----------+--------+-------+");

        Ok(())
    }
}
