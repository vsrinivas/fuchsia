// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{format_err, Context as _, Error};
use argh::FromArgs;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_lowpan::ConnectivityState;
use fidl_fuchsia_lowpan_device::{
    DeviceExtraMarker, DeviceExtraProxy, DeviceMarker, DeviceProxy, Protocols,
};
use fidl_fuchsia_lowpan_test::{DeviceTestMarker, DeviceTestProxy};

/// Contains the arguments decoded for the `status` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "status")]
pub struct StatusCommand {}

async fn print_device_status(
    name: &str,
    device: &DeviceProxy,
    device_extra: &DeviceExtraProxy,
    device_test: &DeviceTestProxy,
) -> Result<(), Error> {
    println!("{}", name);

    // TODO: Add support for state, etc., once the observer support is working.

    if let Some(net_types) = device.get_supported_network_types().await.ok() {
        for (i, net_type) in net_types.iter().enumerate() {
            if i == 0 {
                print!("\ttype: ");
            } else {
                print!(", ");
            }
            print!("{}", net_type);
        }
        if !net_types.is_empty() {
            println!();
        }
    }

    let device_state = device.watch_device_state().await?;

    if let Some(x) = device_state.connectivity_state.as_ref() {
        println!("\tstate: {:?}", x);
    }

    if let Some(x) = device_state.role.as_ref() {
        println!("\trole: {:?}", x);
    }

    match device_state.connectivity_state {
        Some(ConnectivityState::Ready)
        | Some(ConnectivityState::Attaching)
        | Some(ConnectivityState::Attached)
        | Some(ConnectivityState::Isolated) => {
            let identity = device_extra.watch_identity().await?;
            println!("\tidentity: {:?}", identity);
        }

        _ => (),
    }

    let current_mac = device_test.get_current_mac_address().await.ok();
    let factory_mac = device_test.get_factory_mac_address().await.ok();

    if let Some(current_mac) = current_mac.as_ref() {
        println!("\tcurr-mac: {}", hex::encode(current_mac));
    }

    if factory_mac.is_some() && factory_mac != current_mac {
        println!("\tfact-mac: {}", hex::encode(factory_mac.unwrap()));
    }

    if let Some(version) = device_test.get_ncp_version().await.ok() {
        println!("\tncp-version: {:?}", version);
    }

    if let Some(channel) = device_test.get_current_channel().await.ok() {
        println!("\tchan: {}", channel);
    }

    if let Some(rssi) = device_test.get_current_rssi().await.ok() {
        println!("\trssi: {}", rssi);
    }

    Ok(())
}

impl StatusCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let lookup = &context.lookup;
        let device_names: Vec<String> = lookup
            .get_devices()
            .await
            .map_err(std::convert::Into::<Error>::into)
            .context("Unable to list LoWPAN devices")?;

        if device_names.is_empty() {
            Err(format_err!("No LoWPAN interfaces present"))
        } else {
            for name in device_names {
                let (client, server) = create_endpoints::<DeviceMarker>()?;
                let (client_extra, server_extra) = create_endpoints::<DeviceExtraMarker>()?;
                let (client_test, server_test) = create_endpoints::<DeviceTestMarker>()?;

                if let Some(e) = lookup
                    .lookup_device(
                        &name,
                        Protocols {
                            device: Some(server),
                            device_extra: Some(server_extra),
                            device_test: Some(server_test),
                        },
                    )
                    .await
                    .err()
                {
                    println!("{}", &name);
                    println!("\terror: {}", e);
                } else {
                    let device = client.into_proxy();

                    if device.is_err() {
                        println!("{}", &name);
                        println!("\terror: {}", device.unwrap_err());
                        continue;
                    }

                    let device_extra = client_extra.into_proxy();

                    if device_extra.is_err() {
                        println!("{}", &name);
                        println!("\terror: {}", device_extra.unwrap_err());
                        continue;
                    }

                    let device_diags = client_test.into_proxy();

                    if device_diags.is_err() {
                        println!("{}", &name);
                        println!("\terror: {}", device_diags.unwrap_err());
                        continue;
                    }

                    if let Some(e) = print_device_status(
                        &name,
                        &device.unwrap(),
                        &device_extra.unwrap(),
                        &device_diags.unwrap(),
                    )
                    .await
                    .err()
                    {
                        println!("\terror: {}", e);
                    }
                }

                println!();
            }
            Ok(())
        }
    }
}
