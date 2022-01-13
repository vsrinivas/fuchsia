// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    fidl::endpoints::Proxy,
    fidl_fuchsia_driver_test as fdt, fuchsia_async as fasync,
    fuchsia_component_test::RealmBuilder,
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
};

#[fasync::run_singlethreaded(test)]
async fn test_sample_driver() -> Result<()> {
    // Create the RealmBuilder.
    let builder = RealmBuilder::new().await?;
    builder.driver_test_realm_setup().await?;
    // Build the Realm.
    let instance = builder.build().await?;
    // Start DriverTestRealm
    let args = fdt::RealmArgs {
        root_driver: Some("fuchsia-boot:///#meta/test-parent-sys.cm".to_string()),
        use_driver_framework_v2: Some(true),
        ..fdt::RealmArgs::EMPTY
    };
    instance.driver_test_realm_start(args).await?;

    // Connect to our driver.
    let dev = instance.driver_test_realm_connect_to_dev()?;
    let node = device_watcher::recursive_wait_and_open_node(&dev, "compat/sample_driver").await?;

    // Turn the Node connection into the driver's FIDL.
    let driver = fidl_fuchsia_hardware_sample::EchoProxy::new(node.into_channel().unwrap());

    // Call a FIDL method on the driver.
    let response = driver.echo_string("Hello world!").await.unwrap();

    // Verify the response.
    assert!(response == "Hello world!");
    Ok(())
}
