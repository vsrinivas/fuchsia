// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    fidl_fuchsia_driver_test as fdt, fuchsia_async as fasync,
    fuchsia_component_test::builder::RealmBuilder,
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
};

#[fasync::run_singlethreaded(test)]
async fn test_empty_args() -> Result<()> {
    // Create the RealmBuilder.
    let mut realm = RealmBuilder::new().await?;
    realm.driver_test_realm_setup().await?;
    // Build the Realm.
    let instance = realm.build().create().await?;
    // Start DriverTestRealm
    instance.driver_test_realm_start(fdt::RealmArgs::EMPTY).await?;
    // Connect to our driver.
    let dev = instance.driver_test_realm_connect_to_dev()?;
    device_watcher::recursive_wait_and_open_node(&dev, "sys/test/test").await?;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_platform_bus() -> Result<()> {
    // Create the RealmBuilder.
    let mut realm = RealmBuilder::new().await?;
    realm.driver_test_realm_setup().await?;
    // Build the Realm.
    let instance = realm.build().create().await?;
    // Start DriverTestRealm.
    let args = fdt::RealmArgs {
        root_driver: Some("fuchsia-boot:///#driver/platform-bus.so".to_string()),
        ..fdt::RealmArgs::EMPTY
    };
    instance.driver_test_realm_start(args).await?;
    // Connect to our driver.
    let dev = instance.driver_test_realm_connect_to_dev()?;
    device_watcher::recursive_wait_and_open_node(&dev, "sys/platform").await?;
    Ok(())
}
