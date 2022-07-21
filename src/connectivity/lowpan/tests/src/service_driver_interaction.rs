// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fasync::Time;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_lowpan::DeviceWatcherMarker;
use fidl_fuchsia_lowpan_device::{
    DeviceConnectorMarker, DeviceExtraConnectorMarker, DeviceExtraMarker, DeviceMarker,
};
use fidl_fuchsia_lowpan_test::{DeviceTestConnectorMarker, DeviceTestMarker};
use fuchsia_async as fasync;
use fuchsia_async::TimeoutExt;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_component_test::ScopedInstanceFactory;
use futures::prelude::*;

const DEFAULT_TIMEOUT: fuchsia_zircon::Duration = fuchsia_zircon::Duration::from_seconds(50);

#[fasync::run_singlethreaded(test)]
async fn test_service_driver_interaction() -> Result<(), Error> {
    const IFACE_NAME: &str = "lowpan0";

    // Step 1: Get an instance of the DeviceWatcher API and make sure there are no devices registered.
    let lookup = connect_to_protocol::<DeviceWatcherMarker>()
        .context("Failed to connect to Lowpan DeviceWatcher service")?;

    let (added, removed) = lookup
        .watch_devices()
        .err_into::<Error>()
        .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
            Err(format_err!("Timeout waiting for lookup.watch_devices()"))
        })
        .await
        .context("Initial call to lookup.watch_devices() failed")?;

    assert!(added.is_empty() && removed.is_empty(), "Initial device list not empty");

    // Step 2: Start the LoWPAN Dummy Driver
    let dummy_driver = ScopedInstanceFactory::new("drivers")
        .new_instance("#meta/lowpan-dummy-driver.cm")
        .await
        .context("creating lowpan dummy driver")?;
    dummy_driver.connect_to_binder()?;

    // Step 3: Wait to receive an event that the driver has registered.
    let (added, removed) = lookup
        .watch_devices()
        .err_into::<Error>()
        .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
            Err(format_err!("Timeout waiting for lookup.watch_devices()"))
        })
        .await
        .context("Second call to lookup.watch_devices() failed")?;

    assert_eq!(added, vec![IFACE_NAME.to_string()]);
    assert_eq!(removed, Vec::<String>::new());

    // Step 4: Try to lookup the dummy device via the Lookup API
    let (client, server) = create_endpoints::<DeviceMarker>()?;
    let (client_extra, server_extra) = create_endpoints::<DeviceExtraMarker>()?;
    let (client_test, server_test) = create_endpoints::<DeviceTestMarker>()?;

    connect_to_protocol::<DeviceConnectorMarker>()
        .context("Failed to connect to DeviceConnector")?
        .connect(IFACE_NAME, server)?;

    connect_to_protocol::<DeviceExtraConnectorMarker>()
        .context("Failed to connect to DeviceExtraConnector")?
        .connect(IFACE_NAME, server_extra)?;

    connect_to_protocol::<DeviceTestConnectorMarker>()
        .context("Failed to connect to DeviceTestConnector")?
        .connect(IFACE_NAME, server_test)?;

    let device = client.into_proxy()?;
    let device_extra = client_extra.into_proxy()?;
    let device_test = client_test.into_proxy()?;

    // Step 5: Interact with the device to make sure it is responsive.
    let ncp_version = device_test
        .get_ncp_version()
        .map_err(|e| format_err!("Err: {:?}", e))
        .on_timeout(Time::after(DEFAULT_TIMEOUT), || Err(format_err!("Timeout")))
        .await
        .context("Call to get_ncp_version failed")?;

    println!("Got NCP Version: {:?}", ncp_version);

    // Step 5b: Check a method from each protocol to make sure they are all working.
    assert!(device.leave_network().await.is_ok());
    assert!(device_extra.watch_identity().await.is_ok());
    assert!(device_test.get_ncp_version().await.is_ok());

    // Step 6: Kill the driver.
    drop(dummy_driver);

    // Step 7: Make sure that the service doesn't have the device anymore.
    let (added, removed) = lookup
        .watch_devices()
        .err_into::<Error>()
        .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
            Err(format_err!("Timeout waiting for lookup.get_devices()"))
        })
        .await
        .context("Second call to lookup.watch_devices() failed")?;

    assert_eq!(added, Vec::<String>::new());
    assert_eq!(removed, vec![IFACE_NAME.to_string()]);

    // Step 8: Make sure that the endpoints are dead.
    assert!(
        device.leave_network().await.is_err(),
        "Driver killed, but leave_network() still worked!?"
    );
    assert!(
        device_test.get_ncp_version().await.is_err(),
        "Driver killed, but get_ncp_version() still worked!?"
    );

    Ok(())
}
