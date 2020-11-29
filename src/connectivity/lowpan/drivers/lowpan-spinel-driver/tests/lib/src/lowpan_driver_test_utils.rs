// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fasync::Time,
    fidl_fuchsia_lowpan_device::LookupMarker,
    fuchsia_async as fasync,
    fuchsia_async::TimeoutExt,
    fuchsia_component::client::{connect_to_service, launch, launcher, App},
    futures::prelude::*,
};

const DEFAULT_TIMEOUT: fuchsia_zircon::Duration = fuchsia_zircon::Duration::from_seconds(50);

pub async fn lowpan_driver_init() -> App {
    let lookup =
        connect_to_service::<LookupMarker>().expect("Failed to connect to Lowpan Lookup service");

    let devices = lookup
        .watch_devices()
        .err_into::<Error>()
        .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
            Err(format_err!("Timeout waiting for lookup.watch_devices()"))
        })
        .await
        .expect("Initial call to lookup.watch_devices() failed");

    assert!(devices.added.is_empty(), "Initial device list not empty");
    assert!(devices.removed.is_empty(), "Initial device watch had removed devices");

    // Start a LoWPAN spinel Driver
    println!("Starting lowpan spinel driver");
    let launcher = launcher().expect("start launcher");
    let driver_url =
        "fuchsia-pkg://fuchsia.com/lowpan-spinel-driver#meta/lowpan-spinel-driver.cmx".to_string();
    let arg = Some(vec!["--integration".to_string()]);
    let driver = launch(&launcher, driver_url, arg).expect("launch lowpan driver");

    // Wait to receive an event that the driver has registered.
    let devices = lookup
        .watch_devices()
        .err_into::<Error>()
        .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
            Err(format_err!("Timeout waiting for lookup.watch_devices()"))
        })
        .await
        .expect("Second call to lookup.watch_devices() failed");

    assert_eq!(devices.added, vec!["lowpan0".to_string()]);
    assert!(devices.removed.is_empty(), "Second device watch had removed devices");
    driver
}

pub async fn lowpan_driver_deinit(mut driver: App) {
    let lookup =
        connect_to_service::<LookupMarker>().expect("Failed to connect to Lowpan Lookup service");

    let devices = lookup
        .watch_devices()
        .err_into::<Error>()
        .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
            Err(format_err!("Timeout waiting for lookup.watch_devices()"))
        })
        .await
        .expect("Deinit first call to watch_devices() failed");

    assert_eq!(devices.added, vec!["lowpan0".to_string()]);
    assert!(devices.removed.is_empty(), "Second device watch had removed devices");

    // Kill the spinel driver.
    driver.kill().expect("Unable to kill driver");

    // Wait to receive an event that the driver has unregistered.
    let devices = lookup
        .watch_devices()
        .err_into::<Error>()
        .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
            Err(format_err!("Timeout waiting for lookup.watch_devices()"))
        })
        .await
        .expect("Final call to lookup.watch_devices() failed");

    assert!(devices.added.is_empty(), "Final device watch had added devices");
    assert_eq!(devices.removed, vec!["lowpan0".to_string()]);
}

pub async fn call_lowpanctl_cmd(args: Vec<String>) {
    println!("Calling lowpanctl with {:?}", args);
    let launcher = launcher().expect("start launcher");
    let lowpanctl_url = "fuchsia-pkg://fuchsia.com/lowpanctl#meta/lowpanctl.cmx";
    let lowpanctl_cmd =
        launch(&launcher, lowpanctl_url.to_string(), Some(args)).expect("launch lowpanctl");

    println!("Waiting for \"lowpanctl\" to complete");
    let output = lowpanctl_cmd
        .wait_with_output()
        .on_timeout(Time::after(DEFAULT_TIMEOUT), || Err(format_err!("Timeout")))
        .await
        .expect("waiting for lowpanctl to finish");

    println!("Command \"lowpanctl\" completed, {:?}", output.ok());
}

pub fn get_interface_id(
    name: &str,
    intf: &Vec<fidl_fuchsia_net_stack::InterfaceInfo>,
) -> Result<u64, Error> {
    let res = intf
        .iter()
        .find_map(
            |interface| if interface.properties.name == name { Some(interface.id) } else { None },
        )
        .ok_or(anyhow::format_err!("failed to find {}", name))?;
    Ok(res)
}
