// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use crate::prelude::*;
use fidl_fuchsia_lowpan_test::*;

/// Contains the arguments decoded for the `get-mac-filter-settings` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get-mac-filter-settings")]
pub struct GetMacFilterSettingsCommand {}

impl GetMacFilterSettingsCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let device_test_proxy = context
            .get_default_device_test_proxy()
            .await
            .context("Unable to get device instance")?;

        let result = device_test_proxy.get_mac_address_filter_settings().await?;

        let mode_str = match result.mode {
            Some(MacAddressFilterMode::Allow) => "Allow",
            Some(MacAddressFilterMode::Deny) => "Deny",
            Some(MacAddressFilterMode::Disabled) => "Disabled",
            _ => "Unknown",
        };

        println!("mode: {}", mode_str);
        println!("+------------------+-----------+");
        println!("|      HW Addr     |    RSSI   |");

        if let Some(filter_items) = result.items {
            for item in filter_items {
                let hwaddr = format!("{:^16}", get_hex_string(&item.mac_address.unwrap().octets));
                let rssi = if let Some(rssi_val) = item.rssi {
                    format!("{:^9}", rssi_val)
                } else {
                    format!("   None  ")
                };
                println!("+------------------+-----------+");
                println!("| {} | {} |", hwaddr, rssi);
            }
        }
        println!("+------------------+-----------+");

        Ok(())
    }
}
