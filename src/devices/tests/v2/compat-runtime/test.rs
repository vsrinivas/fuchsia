// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    fidl::endpoints::Proxy,
    fidl_fuchsia_compat_runtime_test as ft, fidl_fuchsia_driver_test as fdt,
    fuchsia_async as fasync,
    fuchsia_component_test::RealmBuilder,
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
};

#[fasync::run_singlethreaded(test)]
async fn test_compat_runtime() -> Result<()> {
    // Create the RealmBuilder.
    let builder = RealmBuilder::new().await?;
    builder.driver_test_realm_manifest_setup("#meta/realm.cm").await?;
    let instance = builder.build().await?;
    // Start the DriverTestRealm.
    let args = fdt::RealmArgs {
        root_driver: Some("#meta/root.cm".to_string()),
        use_driver_framework_v2: Some(true),
        ..fdt::RealmArgs::EMPTY
    };
    instance.driver_test_realm_start(args).await?;

    // Connect to our driver.
    let dev = instance.driver_test_realm_connect_to_dev()?;
    let node = device_watcher::recursive_wait_and_open_node(&dev, "root/v1/leaf").await?;
    let driver = ft::LeafProxy::new(node.into_channel().unwrap());
    let response = driver.get_string().await.unwrap();
    assert_eq!(response, "hello world!");
    Ok(())
}
