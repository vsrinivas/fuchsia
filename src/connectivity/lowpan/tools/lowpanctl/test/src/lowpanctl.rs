// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fasync::Time;
use fidl_fuchsia_lowpan_device::LookupMarker;
use fuchsia_async as fasync;
use fuchsia_async::TimeoutExt;
use fuchsia_component::client::{connect_to_service, launch, launcher};
use futures::prelude::*;

const DEFAULT_TIMEOUT: fuchsia_zircon::Duration = fuchsia_zircon::Duration::from_seconds(50);

#[fasync::run_singlethreaded(test)]
pub async fn test_lowpanctl() {
    test_lowpanctl_status().await;
    test_lowpanctl_leave().await;
    test_lowpanctl_reset().await;
    test_lowpanctl_list().await;
    test_lowpanctl_mfg().await;
    test_lowpanctl_provision().await;
    test_lowpanctl_join_network().await;
    test_lowpanctl_form_network().await;
    test_lowpanctl_energy_scan().await;
    test_lowpanctl_network_scan().await;
    test_lowpanctl_set_active().await;
    test_lowpanctl_get_credential().await;
    test_lowpanctl_get_supported_channels().await;
    test_lowpanctl_get_supported_network_types().await;
}

pub async fn test_lowpanctl_status() {
    test_lowpanctl_command(vec!["status".to_string()])
        .await
        .expect("Call to `lowpanctl status` failed.");

    test_lowpanctl_command(vec!["status".to_string(), "--format".to_string(), "csv".to_string()])
        .await
        .expect("Call to `lowpanctl status --format csv` failed.");

    test_lowpanctl_command(vec!["status".to_string(), "--format".to_string(), "std".to_string()])
        .await
        .expect("Call to `lowpanctl status --format std` failed.");
}

pub async fn test_lowpanctl_leave() {
    test_lowpanctl_command(vec!["leave".to_string()])
        .await
        .expect("Call to `lowpanctl leave` failed.");
}

pub async fn test_lowpanctl_reset() {
    test_lowpanctl_command(vec!["reset".to_string()])
        .await
        .expect("Call to `lowpanctl reset` failed.");
}

pub async fn test_lowpanctl_list() {
    test_lowpanctl_command(vec!["list".to_string()])
        .await
        .expect("Call to `lowpanctl list` failed.");
}

pub async fn test_lowpanctl_mfg() {
    test_lowpanctl_command(vec!["mfg".to_string(), "help".to_string()])
        .await
        .expect("Call to `lowpanctl mfg help` failed.");
}

pub async fn test_lowpanctl_provision() {
    test_lowpanctl_command(vec![
        "provision".to_string(),
        "--name".to_string(),
        "some_name".to_string(),
    ])
    .await
    .expect("Call to `lowpanctl provision` failed.");
}

pub async fn test_lowpanctl_energy_scan() {
    test_lowpanctl_command(vec![
        "energy-scan".to_string(),
        "--channels".to_string(),
        "10,100,1000,10000".to_string(),
        "--dwell-time-ms".to_string(),
        "1234567890".to_string(),
    ])
    .await
    .expect("Call to `lowpanctl energy-scan` failed.");
}

pub async fn test_lowpanctl_network_scan() {
    test_lowpanctl_command(vec![
        "network-scan".to_string(),
        "--channels".to_string(),
        "5,50,500,5000".to_string(),
        "--tx-power-dbm".to_string(),
        "-100".to_string(),
    ])
    .await
    .expect("Call to `lowpanctl energy-scan` failed.");
}

pub async fn test_lowpanctl_join_network() {
    test_lowpanctl_command(vec!["join".to_string(), "--name".to_string(), "some_name".to_string()])
        .await
        .expect("Call to `lowpanctl join` failed.");
}

pub async fn test_lowpanctl_form_network() {
    test_lowpanctl_command(vec!["form".to_string(), "--name".to_string(), "some_name".to_string()])
        .await
        .expect("Call to `lowpanctl form` failed.");
}

pub async fn test_lowpanctl_set_active() {
    test_lowpanctl_command(vec!["set-active".to_string(), "true".to_string()])
        .await
        .expect("Call to `lowpanctl set-active` failed.");
}

pub async fn test_lowpanctl_get_credential() {
    test_lowpanctl_command(vec!["get-credential".to_string()])
        .await
        .expect("Call to `lowpanctl get-credential` failed.");
}

pub async fn test_lowpanctl_get_supported_channels() {
    test_lowpanctl_command(vec!["get-supported-channels".to_string()])
        .await
        .expect("Call to `lowpanctl get-supported-channels` failed.");
}

pub async fn test_lowpanctl_get_supported_network_types() {
    test_lowpanctl_command(vec!["get-supported-network-types".to_string()])
        .await
        .expect("Call to `lowpanctl get-supported-network-types` failed.");
}

pub async fn test_lowpanctl_command(args: Vec<String>) -> Result<(), Error> {
    // Step 1: Get an instance of the Lookup API and make sure there are no devices registered.
    let lookup = connect_to_service::<LookupMarker>()
        .context("Failed to connect to Lowpan Lookup service")?;

    let devices = lookup
        .watch_devices()
        .err_into::<Error>()
        .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
            Err(format_err!("Timeout waiting for lookup.watch_devices()"))
        })
        .await
        .context("Initial call to lookup.watch_devices() failed")?;

    assert!(devices.added.is_empty(), "Initial device list not empty");
    assert!(devices.removed.is_empty(), "Initial device watch had removed devices");

    // Step 2: Start a LoWPAN Dummy Driver
    println!("Starting lowpan dummy driver");
    let launcher = launcher()?;
    let driver_url = "fuchsia-pkg://fuchsia.com/lowpan-dummy-driver#meta/lowpan-dummy-driver.cmx";
    let mut driver =
        launch(&launcher, driver_url.to_string(), None).context("launch dummy driver")?;

    // Step 3: Wait to receive an event that the driver has registered.
    let devices = lookup
        .watch_devices()
        .err_into::<Error>()
        .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
            Err(format_err!("Timeout waiting for lookup.watch_devices()"))
        })
        .await
        .context("Second call to lookup.watch_devices() failed")?;

    assert_eq!(devices.added, vec!["lowpan0".to_string()]);
    assert!(devices.removed.is_empty(), "Second device watch had removed devices");

    // Step 4: Call lowpanctl
    println!("Calling lowpanctl with {:?}", args);
    let lowpanctl_url = "fuchsia-pkg://fuchsia.com/lowpanctl#meta/lowpanctl.cmx";
    let lowpanctl_cmd =
        launch(&launcher, lowpanctl_url.to_string(), Some(args)).context("launch lowpanctl")?;

    println!("Waiting for \"lowpanctl\" to complete");
    let output = lowpanctl_cmd
        .wait_with_output()
        .on_timeout(Time::after(DEFAULT_TIMEOUT), || Err(format_err!("Timeout")))
        .await
        .context("waiting for lowpanctl to finish")?;

    println!("Command \"lowpanctl\" completed, {:?}", output.ok());

    // Step 5: Kill the dummy driver.
    driver.kill().context("Unable to kill driver")?;

    // Step 6: Wait to receive an event that the driver has unregistered.
    let devices = lookup
        .watch_devices()
        .err_into::<Error>()
        .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
            Err(format_err!("Timeout waiting for lookup.watch_devices()"))
        })
        .await
        .context("Final call to lookup.watch_devices() failed")?;

    assert!(devices.added.is_empty(), "Final device watch had added devices");
    assert_eq!(devices.removed, vec!["lowpan0".to_string()]);

    output.ok()?;

    Ok(())
}
