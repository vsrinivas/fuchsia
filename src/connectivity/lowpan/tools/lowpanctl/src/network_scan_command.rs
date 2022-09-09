// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use crate::prelude::*;
use fidl_fuchsia_lowpan_experimental::{BeaconInfoStreamMarker, NetworkScanParameters};

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
    pub tx_power_dbm: Option<i8>,
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
            ..NetworkScanParameters::EMPTY
        })
    }

    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let network_scan_marker = self.get_network_scan_params()?;
        let device_extra = context
            .get_default_experimental_device_extra()
            .await
            .context("Unable to get device instance")?;
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
                let network_name = item
                    .identity
                    .as_ref()
                    .map(|x| x.raw_name.as_ref().map(Vec::as_slice))
                    .flatten()
                    .map(|x| {
                        std::str::from_utf8(x)
                            .map(|x| format!("{:?}", x))
                            .unwrap_or_else(|_| hex::encode(x))
                    })
                    .unwrap_or_else(|| String::new());

                let panid = item
                    .identity
                    .as_ref()
                    .map(|x| x.panid)
                    .flatten()
                    .map(|x| format!("{:#04X}", x))
                    .unwrap_or_else(|| "N/A".to_string());

                let ch = item
                    .identity
                    .as_ref()
                    .map(|x| x.channel)
                    .flatten()
                    .map(|x| x.to_string())
                    .unwrap_or_else(|| "N/A".to_string());

                let xpanid = item
                    .identity
                    .as_ref()
                    .map(|x| x.xpanid.as_ref().map(Vec::as_slice))
                    .flatten()
                    .map(hex::encode)
                    .unwrap_or_else(|| "N/A".to_string());

                let hwaddr = item
                    .address
                    .as_ref()
                    .map(|fidl_fuchsia_lowpan::MacAddress { octets }| octets)
                    .map(hex::encode)
                    .unwrap_or_else(|| "N/A".to_string());

                let rssi = item.rssi.map(|x| x.to_string()).unwrap_or_else(|| "N/A".to_string());

                println!(
                    "| {:^17} | {:^6} | {:^2} | {:^16} | {:^16} | {:^9} |",
                    network_name, panid, ch, xpanid, hwaddr, rssi
                );
            }
        }
        Ok(())
    }
}
