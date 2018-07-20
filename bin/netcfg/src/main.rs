// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
#[macro_use]
extern crate serde_derive;
extern crate serde_json;
extern crate fidl;
extern crate fidl_fuchsia_devicesettings;
extern crate fidl_fuchsia_netstack as netstack;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;

use failure::{Error, ResultExt};
use fidl_fuchsia_devicesettings::{DeviceSettingsManagerMarker};
use netstack::{NetstackMarker, NetInterface, NetstackEvent, INTERFACE_FEATURE_SYNTH, INTERFACE_FEATURE_LOOPBACK};
use std::fs;
use std::io::Read;
use futures::{future, FutureExt, StreamExt};

mod device_id;

const DEFAULT_CONFIG_FILE: &str = "/pkg/data/default.json";

#[derive(Debug, Deserialize)]
pub struct Config {
    pub device_name: Option<String>,
}

fn parse_config(config: String) -> Result<Config, Error> {
    serde_json::from_str(&config).map_err(Into::into)
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

// Workaround for https://fuchsia.atlassian.net/browse/TC-141
fn read_to_string(s: &str) -> Result<String, Error> {
    let mut f = fs::File::open(s).context("Failed to read file")?;
    let mut out = String::new();
    f.read_to_string(&mut out)?;
    Ok(out)
}

static DEVICE_NAME_KEY: &str = "DeviceName";

fn main() -> Result<(), Error> {
    println!("netcfg: started");
    let default_config = parse_config(read_to_string(DEFAULT_CONFIG_FILE)?)?;
    let mut executor = async::Executor::new().context("error creating event loop")?;
    let netstack = app::client::connect_to_service::<NetstackMarker>().context("failed to connect to netstack")?;
    let device_settings_manager = app::client::connect_to_service::<DeviceSettingsManagerMarker>()
        .context("failed to connect to device settings manager")?;

    let f = match default_config.device_name {
        Some(name) => device_settings_manager.set_string(DEVICE_NAME_KEY, &name).left_future(),
        None =>
            netstack
                .take_event_stream()
                .filter_map(|NetstackEvent::InterfacesChanged { interfaces: is }| future::ok(derive_device_name(is)))
                .next()
                .map_err(|(e, _)| e)
                .and_then(|(opt, _)| {
                    match opt {
                        Some(name) => device_settings_manager.set_string(DEVICE_NAME_KEY, &name).left_future(),
                        None => future::ok(false).right_future(),
                    }
                })
                .right_future()
    };

    let _ = executor.run_singlethreaded(f)?;

    Ok(())
}
