// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    fidl::endpoints::Proxy,
    fidl_fuchsia_driver_test as fdt, fuchsia_async as fasync,
    fuchsia_component_test::new::RealmBuilder,
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    fuchsia_zircon as zx,
};

// This test checks for a very specific bug in the compat driver, where
// adding devices would fail if it was in the same driver in the same driver host,
// even if those two drivers had different instances.
#[fasync::run_singlethreaded(test)]
async fn test_adding_children() -> Result<()> {
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

    // Connect to our root-a/leaf driver.
    let dev = instance.driver_test_realm_connect_to_dev()?;
    let node = device_watcher::recursive_wait_and_open_node(&dev, "sys/test/root-a/leaf").await?;
    let driver = fidl_fuchsia_hardware_compat::LeafProxy::new(node.into_channel().unwrap());

    // Make sure we can add a child.
    let response = driver.add_child("child").await.unwrap();
    assert_eq!(response, zx::Status::OK.into_raw());

    // Connect to our root-b/leaf driver.
    let dev = instance.driver_test_realm_connect_to_dev()?;
    let node = device_watcher::recursive_wait_and_open_node(&dev, "sys/test/root-b/leaf").await?;
    let driver = fidl_fuchsia_hardware_compat::LeafProxy::new(node.into_channel().unwrap());

    // Make sure we can add a child with the *same name* that we added
    // to root-a/leaf.
    let response = driver.add_child("child").await.unwrap();
    assert_eq!(response, zx::Status::OK.into_raw());

    // Check that both children are in /dev/.
    device_watcher::recursive_wait_and_open_node(&dev, "sys/test/root-a/leaf/child").await?;
    device_watcher::recursive_wait_and_open_node(&dev, "sys/test/root-b/leaf/child").await?;

    Ok(())
}

// This test checks that a driver shares globals with the same driver in
// the same driver host.
#[fasync::run_singlethreaded(test)]
async fn test_sharing_globals() -> Result<()> {
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

    // Connect to our root-a/leaf driver.
    let dev = instance.driver_test_realm_connect_to_dev()?;
    let node = device_watcher::recursive_wait_and_open_node(&dev, "sys/test/root-a/leaf").await?;
    let driver = fidl_fuchsia_hardware_compat::LeafProxy::new(node.into_channel().unwrap());

    // Our global should be 0, and we are incrementing it to 1.
    let counter = driver.global_counter().await.unwrap();
    assert_eq!(counter, 0);

    // Connect to our root-b/leaf driver.
    let dev = instance.driver_test_realm_connect_to_dev()?;
    let node = device_watcher::recursive_wait_and_open_node(&dev, "sys/test/root-b/leaf").await?;
    let driver = fidl_fuchsia_hardware_compat::LeafProxy::new(node.into_channel().unwrap());

    // Our global should be 1 (since root-a incremented it).
    let counter = driver.global_counter().await.unwrap();
    assert_eq!(counter, 1);

    Ok(())
}
