// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{Context, Error};
use argh::FromArgs;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_lowpan_device::{BeaconInfoStreamMarker, NetworkScanParameters};

/// Contains the arguments decoded for the `network-scan` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "network-scan")]
pub struct NetworkScanCommand {
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
    pub tx_power_dbm: Option<i32>,
}

impl NetworkScanCommand {
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

    fn get_network_scan_params(&self) -> Result<NetworkScanParameters, Error> {
        Ok(NetworkScanParameters {
            channels: self.get_channels_vec_from_str()?,
            tx_power_dbm: self.tx_power_dbm.clone(),
            ..NetworkScanParameters::empty()
        })
    }

    fn get_hex_string(&self, vec: &Vec<u8>) -> String {
        let mut string = String::from("");
        for item in vec {
            string.push_str(&format!("{:02x}", item)[..]);
        }
        string
    }

    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let network_scan_marker = self.get_network_scan_params()?;
        let (_, device_extra, _) =
            context.get_default_device_proxies().await.context("Unable to get device instance")?;
        let (client_end, server_end) = create_endpoints::<BeaconInfoStreamMarker>()?;
        let result_stream = client_end.into_proxy()?;
        device_extra
            .start_network_scan(network_scan_marker, server_end)
            .context("Unable to send start network scan command")?;
        println!("result(s):");
        println!(
            "|-------------------+--------+----+------------------+------------------+-----------|"
        );
        println!(
            "|   NetworkName     | PAN ID | Ch |      XPanID      |   HWAddr         |    RSSI   |"
        );

        loop {
            let vec = result_stream.next().await?;
            if vec.is_empty() {
                println!("|-------------------+--------+----+------------------+------------------+-----------|");
                break;
            }
            println!("|-------------------+--------+----+------------------+------------------+-----------|");
            for item in vec {
                let network_name_vec = match item.identity.raw_name {
                    Some(x) => x,
                    None => Vec::new(),
                };
                let network_name = match std::str::from_utf8(&network_name_vec) {
                    Ok(x) => format!("{:?}", x),
                    Err(_) => format!("{:?}", self.get_hex_string(&network_name_vec)),
                };
                let panid = format!(
                    "{:^6}",
                    match item.identity.panid {
                        Some(num) => format_args!("{:#04X}", num).to_string(),
                        None => format_args!("N/A").to_string(),
                    }
                );
                let ch = format!(
                    "{:^2}",
                    match item.identity.channel {
                        Some(num) => num.to_string(),
                        None => format_args!("N/A").to_string(),
                    }
                );
                let xpanid_vec = match item.identity.xpanid {
                    Some(x) => x,
                    None => Vec::new(),
                };
                let xpanid = format!("{:^16}", self.get_hex_string(&xpanid_vec));
                let hwaddr = format!("{:^16}", self.get_hex_string(&item.address));
                let rssi = format!("{:^9}", item.rssi);
                println!(
                    "| {:^17} | {} | {} | {} | {} | {} |",
                    network_name, panid, ch, xpanid, hwaddr, rssi
                );
            }
        }
        Ok(())
    }
}
