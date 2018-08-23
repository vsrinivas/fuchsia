// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use fuchsia_async::temp::TempFutureExt;
use failure::{Error, ResultExt};
use fidl_fuchsia_devicesettings::{DeviceSettingsManagerMarker};
use fidl_fuchsia_netstack::{NetstackMarker, NetAddress, Ipv4Address, Ipv6Address, NetAddressFamily, NetInterface, NetstackEvent, INTERFACE_FEATURE_SYNTH, INTERFACE_FEATURE_LOOPBACK};
use serde_derive::Deserialize;
use std::fs;
use std::net::IpAddr;
use futures::{future, StreamExt, TryFutureExt, TryStreamExt};

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
    (n.features & (INTERFACE_FEATURE_SYNTH | INTERFACE_FEATURE_LOOPBACK)) == 0
}

fn derive_device_name(interfaces: Vec<NetInterface>) -> Option<String> {
    interfaces.iter()
        .filter(|iface| is_physical(iface))
        .min_by(|a, b| a.id.cmp(&b.id))
        .map(|iface| device_id::device_id(&iface.hwaddr))
}

static DEVICE_NAME_KEY: &str = "DeviceName";

fn main() -> Result<(), Error> {
    println!("netcfg: started");
    let default_config_file = fs::File::open(DEFAULT_CONFIG_FILE)?;
    let default_config: Config = serde_json::from_reader(default_config_file)?;
    let mut executor = fuchsia_async::Executor::new().context("error creating event loop")?;
    let netstack = fuchsia_app::client::connect_to_service::<NetstackMarker>().context("failed to connect to netstack")?;
    let device_settings_manager = fuchsia_app::client::connect_to_service::<DeviceSettingsManagerMarker>()
        .context("failed to connect to device settings manager")?;

    let device_name = match default_config.device_name {
        Some(name) => {
            device_settings_manager.set_string(DEVICE_NAME_KEY, &name).map_ok(|_| ()).left_future()
        },
        None => {
            netstack.take_event_stream().try_filter_map(|NetstackEvent::OnInterfacesChanged { interfaces: is }| {
                future::ready(Ok(derive_device_name(is)))
            }).take(1).try_for_each(|name| {
                device_settings_manager.set_string(DEVICE_NAME_KEY, &name).map_ok(|_| ())
            }).map_ok(|_| ()).right_future()
        },
    }.map_err(Into::into);

    let mut servers = default_config.dns_config.servers.iter().map(to_net_address).collect::<Vec<NetAddress>>();
    let () = netstack.set_name_servers(&mut servers.iter_mut())?;

    executor.run_singlethreaded(device_name)
}

fn to_net_address(addr: &IpAddr) -> NetAddress {
    match addr {
        IpAddr::V4(v4addr) => NetAddress{
            family: NetAddressFamily::Ipv4,
            ipv4: Some(Box::new(Ipv4Address { addr: v4addr.octets() })),
            ipv6: None,
        },
        IpAddr::V6(v6addr) => NetAddress{
            family: NetAddressFamily::Ipv6,
            ipv4: None,
            ipv6: Some(Box::new(Ipv6Address { addr: v6addr.octets() })),
        }
    }
}
