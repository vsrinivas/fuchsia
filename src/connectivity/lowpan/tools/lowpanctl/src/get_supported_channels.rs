// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{Context as _, Error};
use argh::FromArgs;

/// Contains the arguments decoded for the `get-supported-channels` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get-supported-channels")]
pub struct GetSupportedChannelsCommand {}

impl GetSupportedChannelsCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let device = context.get_default_device().await.context("Unable to get device instance")?;

        let channel_infos = device
            .get_supported_channels()
            .await
            .context("Unable to send get_supported_channels command")?;

        println!("+-----------+------------+--------------+----------------------+--------------------+-----------------------------+");
        println!("|   Index   |     Id     | Max Tx Power | Spectrum Center Freq | Spectrum Bandwidth | Masked by Regulatory Domain |");
        println!("+-----------+------------+--------------+----------------------+--------------------+-----------------------------+");

        for channel_info in channel_infos {
            let index = match channel_info.index {
                Some(x) => format!("{:^11}", x),
                None => format!("{:^11}", "N/A".to_string()),
            };
            let id = match channel_info.id {
                Some(x) => format!("{:?}", x),
                None => format!("{}", "N/A".to_string()),
            };
            let max_transmit_power = match channel_info.max_transmit_power {
                Some(x) => format!("{:^14}", x),
                None => format!("{:^14}", "N/A".to_string()),
            };
            let spectrum_center_frequency = match channel_info.spectrum_center_frequency {
                Some(x) => format!("{:^22}", x),
                None => format!("{:^22}", "N/A".to_string()),
            };
            let spectrum_bandwidth = match channel_info.spectrum_bandwidth {
                Some(x) => format!("{:^20}", x),
                None => format!("{:^20}", "N/A".to_string()),
            };
            let masked_by_regulatory_domain = match channel_info.masked_by_regulatory_domain {
                Some(x) => format!("{:^29}", x),
                None => format!("{:^29}", "N/A".to_string()),
            };
            println!(
                "|{}|{:^12}|{}|{}|{}|{}|",
                index,
                id,
                max_transmit_power,
                spectrum_center_frequency,
                spectrum_bandwidth,
                masked_by_regulatory_domain
            );
        }

        println!("+-----------+------------+--------------+----------------------+--------------------+-----------------------------+");

        Ok(())
    }
}
