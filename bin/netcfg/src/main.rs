// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro)]

use std::borrow::Cow;
use std::fs;
use std::net::IpAddr;

use failure::{self, ResultExt};
use futures::{self, StreamExt, TryStreamExt};
use serde_derive::Deserialize;

use fidl_fuchsia_devicesettings::DeviceSettingsManagerMarker;
use fidl_fuchsia_netstack::{Ipv4Address, Ipv6Address, NetAddress, NetAddressFamily, NetInterface,
                            NetstackEvent, NetstackMarker};
use fidl_zircon_ethernet::{INFO_FEATURE_LOOPBACK, INFO_FEATURE_SYNTH};

mod device_id;

const DEFAULT_CONFIG_FILE: &str = "/pkg/data/default.json";

#[derive(Debug, Deserialize)]
struct Config {
    pub device_name: Option<String>,
    pub dns_config: DnsConfig,
}

#[derive(Debug, Deserialize)]
struct DnsConfig {
    pub servers: Vec<IpAddr>,
}

fn is_physical(n: &NetInterface) -> bool {
    (n.features & (INFO_FEATURE_SYNTH | INFO_FEATURE_LOOPBACK)) == 0
}

fn derive_device_name(interfaces: Vec<NetInterface>) -> Option<String> {
    interfaces
        .iter()
        .filter(|iface| is_physical(iface))
        .min_by(|a, b| a.id.cmp(&b.id))
        .map(|iface| device_id::device_id(&iface.hwaddr))
}

static DEVICE_NAME_KEY: &str = "DeviceName";

fn main() -> Result<(), failure::Error> {
    println!("netcfg: started");
    let default_config_file = fs::File::open(DEFAULT_CONFIG_FILE)?;
    let default_config: Config = serde_json::from_reader(default_config_file)?;
    let mut executor = fuchsia_async::Executor::new().context("error creating event loop")?;
    let netstack = fuchsia_app::client::connect_to_service::<NetstackMarker>()
        .context("failed to connect to netstack")?;
    let device_settings_manager = fuchsia_app::client::connect_to_service::<
        DeviceSettingsManagerMarker,
    >().context("failed to connect to device settings manager")?;

    let mut servers = default_config
        .dns_config
        .servers
        .iter()
        .map(to_net_address)
        .collect::<Vec<NetAddress>>();
    let () = netstack.set_name_servers(&mut servers.iter_mut())?;

    let default_device_name = default_config
        .device_name
        .as_ref()
        .map(Cow::Borrowed)
        .map(Ok);

    let mut device_name_stream = futures::stream::iter(default_device_name).chain(
        netstack.take_event_stream().try_filter_map(
            |NetstackEvent::OnInterfacesChanged { interfaces }| {
                futures::future::ok(derive_device_name(interfaces).map(Cow::Owned))
            },
        ),
    );

    executor.run_singlethreaded(
        async {
            match await!(device_name_stream.try_next())? {
                Some(device_name) => {
                    let _success =
                        await!(device_settings_manager.set_string(DEVICE_NAME_KEY, &device_name))?;
                    Ok(())
                }
                None => Err(failure::err_msg(
                    "netstack event stream ended without providing interfaces",
                )),
            }
        },
    )
}

fn to_net_address(addr: &IpAddr) -> NetAddress {
    match addr {
        IpAddr::V4(v4addr) => NetAddress {
            family: NetAddressFamily::Ipv4,
            ipv4: Some(Box::new(Ipv4Address {
                addr: v4addr.octets(),
            })),
            ipv6: None,
        },
        IpAddr::V6(v6addr) => NetAddress {
            family: NetAddressFamily::Ipv6,
            ipv4: None,
            ipv6: Some(Box::new(Ipv6Address {
                addr: v6addr.octets(),
            })),
        },
    }
}
