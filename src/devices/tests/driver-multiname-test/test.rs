// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    fidl::endpoints::Proxy,
    fidl_fuchsia_driver_test as fdt, fuchsia_async as fasync,
    fuchsia_component_test::new::RealmBuilder,
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
};

#[fasync::run_singlethreaded(test)]
async fn test_sample_driver() -> Result<()> {
    // Start the driver test realm.
    let builder = RealmBuilder::new().await?;
    builder.driver_test_realm_setup().await?;
    let instance = builder.build().await?;
    instance.driver_test_realm_start(fdt::RealmArgs::EMPTY).await?;

    // Connect to our driver.
    let dev = instance.driver_test_realm_connect_to_dev()?;
    let node = device_watcher::recursive_wait_and_open_node(&dev, "sys/test/parent_device").await?;

    // Turn the Node connection into the driver's FIDL.
    let driver = fidl_driver_multiname_test::TestAddDeviceProxy::new(node.into_channel().unwrap());

    // Call a FIDL method to add a device. This should succeed.
    let response = driver.add_device().await?;
    assert!(response.is_ok());

    // Make sure the child device exists.
    let possible_node =
        device_watcher::recursive_wait_and_open_node(&dev, "sys/test/parent_device/duplicate")
            .await;
    assert!(possible_node.is_ok());

    // Call it again to add a second device with the same name, which should fail.
    let response = driver.add_device().await?;
    assert!(response.is_err());
    Ok(())
}
