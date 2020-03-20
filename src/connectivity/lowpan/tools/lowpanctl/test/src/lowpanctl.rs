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
}

pub async fn test_lowpanctl_status() {
    test_lowpanctl_command(vec!["status".to_string()])
        .await
        .expect("Call to `lowpanctl status` failed.");
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
