// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{Context, Error};
use argh::FromArgs;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_lowpan_device::{EnergyScanParameters, EnergyScanResultStreamMarker};

/// Contains the arguments decoded for the `energy-scan` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "energy-scan")]
pub struct EnergyScanCommand {
    /// subset of channels to scan.
    ///
    /// if unspecified, all channels will be scanned.
    #[argh(option)]
    pub channels: Option<String>,
    /// transmit power (in dBm to the antenna) for transmitting
    /// beacon requests.
    ///
    /// note that hardware limitations may cause the actual
    /// used transmit power to differ from what is specified.
    /// In that case the used transmit power will always be
    /// the highest available transmit power that is less than
    /// the specified transmit power. If the desired transmit
    /// power is lower than the lowest transmit power supported
    /// by the hardware, then that will be used instead.
    #[argh(option)]
    pub dwell_time_ms: Option<u32>,
}

impl EnergyScanCommand {
    fn get_channels_vec_from_str(&self) -> Result<Option<Vec<u16>>, Error> {
        self.channels
            .as_ref()
            .map(|value| {
                let chans = value.split(",");
                let mut res_vec: Vec<u16> = Vec::new();
                for chan in chans {
                    let res = u16::from_str_radix(chan, 10)?;
                    res_vec.push(res);
                }
                Ok(res_vec)
            })
            .transpose()
    }

    fn get_energy_scan_params(&self) -> Result<EnergyScanParameters, Error> {
        Ok(EnergyScanParameters {
            channels: self.get_channels_vec_from_str()?,
            dwell_time_ms: self.dwell_time_ms.clone(),
            ..EnergyScanParameters::EMPTY
        })
    }

    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let energy_scan_marker = self.get_energy_scan_params()?;
        let (_, device_extra, _) =
            context.get_default_device_proxies().await.context("Unable to get device instance")?;
        let (client_end, server_end) = create_endpoints::<EnergyScanResultStreamMarker>()?;
        let result_stream = client_end.into_proxy()?;
        println!("{:?}", energy_scan_marker);
        device_extra
            .start_energy_scan(energy_scan_marker, server_end)
            .context("Unable to send start energy scan command")?;
        println!("result(s):");
        println!("|-----------------+------------+------------|");
        println!("|  channel_index  |  max_rssi  |  min_rssi  |");

        loop {
            let vec = result_stream.next().await?;
            if vec.is_empty() {
                println!("|-----------------+------------+------------|");
                break;
            }
            println!("|-----------------+------------+------------|");
            for item in vec {
                let channel_index = match item.channel_index {
                    Some(num) => format_args!("{:6}", num).to_string(),
                    None => format_args!("  N/A ").to_string(),
                };
                let max_rssi = match item.max_rssi {
                    Some(num) => format_args!("{:6}", num).to_string(),
                    None => format_args!("  N/A ").to_string(),
                };
                let min_rssi = match item.min_rssi {
                    Some(num) => format_args!("{:6}", num).to_string(),
                    None => format_args!("  N/A ").to_string(),
                };
                println!("|     {}      |  {}    |  {}    |", channel_index, max_rssi, min_rssi);
            }
        }
        Ok(())
    }
}
